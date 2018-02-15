/* vim: set et ts=3 sw=3 sts=3 ft=c:
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>

extern "C" {
  #include <mosquitto.h>
  #include <mosquitto_plugin.h>
}

#include <thread>
#include <future>
#include <chrono>
#include <iostream>
#include <pq_pool.h>
#include <librdkafka/rdkafka.h>

#include "kafka.pb.h"
#include "syscall_def.h"

/**
 * @brief Message delivery report callback.
 *
 * This callback is called exactly once per message, indicating if
 * the message was succesfully delivered
 * (rkmessage->err == RD_KAFKA_RESP_ERR_NO_ERROR) or permanently
 * failed delivery (rkmessage->err != RD_KAFKA_RESP_ERR_NO_ERROR).
 *
 * The callback is triggered from rd_kafka_poll() and executes on
 * the application's thread.
 */
static void dr_msg_cb (rd_kafka_t *rk, const rd_kafka_message_t *rkmessage, void *opaque) {
  if (rkmessage->err)
    fprintf(stderr, "%% Message delivery failed: %s\n", rd_kafka_err2str(rkmessage->err));
  else
    fprintf(stderr, "%% Message delivered (%zd bytes, " "partition %" PRId32 ")\n", rkmessage->len, rkmessage->partition);

  /* The rkmessage is destroyed automatically by librdkafka */
}

int mosquitto_auth_plugin_version(void) {
	return MOSQ_AUTH_PLUGIN_VERSION;
}

static rd_kafka_t *g_rk = nullptr;         /* Producer instance handle */
static rd_kafka_topic_t *g_rkt = nullptr;  /* Topic object */
static std::thread *g_kafka_thread = nullptr;
static std::promise<bool> g_kafka_promise;

#define KFK_BROKERS "kafka"
#define MQTT_CONNECT_TOPIC "codein"

static void kafka_send(const ::google::protobuf::Message& msg) {
  size_t len = msg.ByteSize();
  auto buf = new char[len];
  msg.SerializeToArray(buf, len);

  retry:
  if (rd_kafka_produce(
        /* Topic object */
        g_rkt,
        /* Use builtin partitioner to select partition*/
        RD_KAFKA_PARTITION_UA,
        /* Make a copy of the payload. */
        RD_KAFKA_MSG_F_COPY,
        /* Message payload (value) and length */
        buf, len,
        /* Optional key and its length */
        NULL, 0,
        /* Message opaque, provided in
         * delivery report callback as
         * msg_opaque. */
        NULL) == -1) {
    /**
     * Failed to *enqueue* message for producing.
     */
    fprintf(stderr,
        "%% Failed to produce to topic %s: %s\n",
        rd_kafka_topic_name(g_rkt),
        rd_kafka_err2str(rd_kafka_last_error()));

    /* Poll to handle delivery reports */
    if (rd_kafka_last_error() == RD_KAFKA_RESP_ERR__QUEUE_FULL) {
      /* If the internal queue is full, wait for
       * messages to be delivered and then retry.
       * The internal queue represents both
       * messages to be sent and messages that have
       * been sent or failed, awaiting their
       * delivery report callback to be called.
       *
       * The internal queue is limited by the
       * configuration property
       * queue.buffering.max.messages */
      rd_kafka_poll(g_rk, 1000/*block for max 1000ms*/);
      goto retry;
    }
  } else {
    fprintf(stderr, "%% Enqueued message (%zd bytes) "
        "for topic %s\n",
        len, rd_kafka_topic_name(g_rkt));
  }

  delete[] buf;
}

static void kafka_run() {
  fprintf(stderr, "kafka thread(%ld) started ...\n", (long)get_tid());
  while(true) {
    rd_kafka_poll(g_rk, 60*1000);
  }
  fprintf(stderr, "%% flushing final messages..\n");
  rd_kafka_flush(g_rk, 10*1000 /* wait for max 10 seconds */);
  g_kafka_promise.set_value(true);
}

static int kafka_init() {
  /*
   * Create Kafka client configuration place-holder
   */
  auto conf = rd_kafka_conf_new();

  /* Set bootstrap broker(s) as a comma-separated list of
   * host or host:port (default port 9092).
   * librdkafka will use the bootstrap brokers to acquire the full
   * set of brokers from the cluster. */
  char errstr[1024];
  if (rd_kafka_conf_set(conf, "bootstrap.servers", KFK_BROKERS,
        errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK) {
    rd_kafka_conf_destroy(conf);
    fprintf(stderr, "%s\n", errstr);
    return -1;
  }

  /* Set the delivery report callback.
   * This callback will be called once per message to inform
   * the application if delivery succeeded or failed.
   * See dr_msg_cb() above. */
  rd_kafka_conf_set_dr_msg_cb(conf, dr_msg_cb);


  /*
   * Create producer instance.
   *
   * NOTE: rd_kafka_new() takes ownership of the conf object
   *       and the application must not reference it again after
   *       this call.
   */
  g_rk = rd_kafka_new(RD_KAFKA_PRODUCER, conf, errstr, sizeof(errstr));
  if (g_rk == nullptr) {
    fprintf(stderr, "%% Failed to create new producer: %s\n", errstr);
    return -1;
  }


  /* Create topic object that will be reused for each message
   * produced.
   *
   * Both the producer instance (rd_kafka_t) and topic objects (topic_t)
   * are long-lived objects that should be reused as much as possible.
   */
  g_rkt = rd_kafka_topic_new(g_rk, MQTT_CONNECT_TOPIC, NULL);
  if (g_rkt == nullptr) {
    fprintf(stderr, "%% Failed to create topic object: %s\n",
        rd_kafka_err2str(rd_kafka_last_error()));
    rd_kafka_destroy(g_rk);
    return -1;
  }

  return 0;
}

static PQpool *g_pq_pool = nullptr;
#define PGQCONNSTR "postgresql://y:jO.sAt1G0T~^95@localhost/postgres"

int mosquitto_auth_plugin_init(void **user_data, struct mosquitto_opt *opts, int opt_count) {
  using namespace std::chrono_literals;
	g_pq_pool = new PQpool(PGQCONNSTR, 2);
	/* Check to see that the backend connection was successfully made */
	if (g_pq_pool == nullptr) {
		fprintf(stderr, "error init pq pool!");
		return MOSQ_ERR_CONN_PENDING;
	}

#ifdef USE_KAFKA
	if (kafka_init() != 0) {
		if (g_pq_pool != nullptr) {
			delete g_pq_pool;
			g_pq_pool = nullptr;
		}
		fprintf(stderr, "error init kafka!");
		return MOSQ_ERR_CONN_PENDING;
	}

  auto future = g_kafka_promise.get_future();
  g_kafka_thread = new std::thread(kafka_run);
  auto status = future.wait_for(300ms);

  if (status == std::future_status::ready) {
	  if (g_pq_pool != nullptr) {
      delete g_pq_pool;
      g_pq_pool = nullptr;
	  }
    std::cerr << "kafka thread terminated!" << std::endl;
    g_kafka_thread->join();
    delete g_kafka_thread;
    g_kafka_thread = nullptr;
		return MOSQ_ERR_CONN_PENDING;
  }

#endif // USE_KAFKA
  return MOSQ_ERR_SUCCESS;
}

int mosquitto_auth_plugin_cleanup(void *user_data, struct mosquitto_opt *opts, int opt_count) {
	if (g_pq_pool != nullptr) {
		delete g_pq_pool;
		g_pq_pool = nullptr;
	}

  if (g_kafka_thread != nullptr) {
    g_kafka_thread->join();
    delete g_kafka_thread;
    g_kafka_thread = nullptr;
  }

  if (g_rkt != nullptr) {
    rd_kafka_topic_destroy(g_rkt);
    g_rkt = nullptr;
  }

  if (g_rk != nullptr) {
    rd_kafka_destroy(g_rk);
    g_rk = nullptr;
  }

  return MOSQ_ERR_SUCCESS;
}

int mosquitto_auth_security_init(void *user_data, struct mosquitto_opt *opts, int opt_count, bool reload) {
	return MOSQ_ERR_SUCCESS;
}

int mosquitto_auth_security_cleanup(void *user_data, struct mosquitto_opt *opts, int opt_count, bool reload) {
	return MOSQ_ERR_SUCCESS;
}

int mosquitto_auth_acl_check(void *user_data, int access, const struct mosquitto *client, struct mosquitto_acl_msg *msg) {
	int rc = MOSQ_ERR_ACL_DENIED;
	if(access == MOSQ_ACL_READ){
	}
	// currently allow any publish request
	rc = MOSQ_ERR_SUCCESS;
	return rc;
}

static int check_token(long uid, const char *token) {
  auto conn = g_pq_pool->Pop();
	if (conn == nullptr) {
    fprintf(stderr, "connect db error!\n");
		return -1;
  }

	char sql[510];
	snprintf(sql, sizeof sql, "select count(1) from app_session "
			"where uid=%ld and session='%s' and expired=0", uid, token);

	PGresult *res = PQexec(conn, sql);
	if (PQresultStatus(res) != PGRES_TUPLES_OK) {
		fprintf(stderr, "select failed: %s", PQerrorMessage(conn));
		PQclear(res);
    g_pq_pool->Push(conn);
		return -1;
	}
	long count = 0L;
	/* next, print out the rows */
	for (int i = 0; i < PQntuples(res); i++) {
		// for (j = 0; j < nFields; j++)
		count = atol(PQgetvalue(res, i, 0));
		if (count >= 0)
			break;
	}

	PQclear(res);
  g_pq_pool->Push(conn);
	return count > 0 ? 0 : -1;
}

static long get_uid(const char *username) {
	if (username[0] == 0) {
		return -1;
	}
	long uid = 0;
	for (int i = 0; username[i] != 0; ++i) {
		int d = username[i] - '0';
		if (d >= 0 && d <= 9) {
			uid *= 10;
			uid += d;
		} else {
			if (d != ' ') {
				return -1;
			}
		}
	}

	return uid;
}

static void on_client_online(long uid, const char *token, const char *clientid, int sd) {
  kafka_proto::MqttStatus msg;
  msg.set_status(kafka_proto::MqttStatus::online);
  msg.set_uid(uid);
  msg.set_token(token);
  msg.set_device(clientid);
  msg.set_time(time(NULL)*1000);
  
  do {
    char ip[100];
    struct sockaddr_in6 addr;
    socklen_t slen = sizeof(addr);
    getpeername(sd, (struct sockaddr *)&addr, &slen);

    char serv[64];
    getnameinfo((struct sockaddr *)&addr, slen, ip, sizeof(ip), serv, sizeof(serv), NI_NUMERICHOST | NI_NUMERICSERV);
    msg.set_addr(ip);
  } while(0);

  kafka_send(msg);
}

static void update_client_status(long uid, const char *token, const char *clientid, int sd) {
  if (clientid == NULL || clientid[0] == 0) {
    fprintf(stderr, "empty client id!\n");
    return;
  }
  auto conn = g_pq_pool->Pop();
	if (conn == nullptr) {
    fprintf(stderr, "connect db error!\n");
		return;
  }

  char ip[100] = {0};
  do {
    struct sockaddr_in6 addr;
    socklen_t slen = sizeof(addr);
    getpeername(sd, (struct sockaddr *)&addr, &slen);

    char serv[64];
    getnameinfo((struct sockaddr *)&addr, slen, ip, sizeof(ip), serv, sizeof(serv), NI_NUMERICHOST | NI_NUMERICSERV);

  } while(0);

  char sql[512];
  snprintf(sql, sizeof sql, "update client_status set status=0,play=0 "
      "where uid=%ld and device!='%s'", uid, clientid);
  PGresult *res = PQexec(conn, sql);
  if (PQresultStatus(res) != PGRES_COMMAND_OK) {
    fprintf(stderr, "%s sql error: %s. sql: %s\n", __func__, PQerrorMessage(conn), sql);
  }

  snprintf(sql, sizeof sql, "update client_status set ip='%s' "
      "where uid=%ld and device='%s'", ip, uid, clientid);
  res = PQexec(conn, sql);
  if (PQresultStatus(res) != PGRES_COMMAND_OK) {
    fprintf(stderr, "%s sql error: %s. sql: %s\n", __func__, PQerrorMessage(conn), sql);
  }

  snprintf(sql, sizeof sql, "update app_session set expired=extract(epoch from now())"
      " where uid=%ld and (session!='%s' or device_id!='%s')", uid, token, clientid);
  res = PQexec(conn, sql);
  if (PQresultStatus(res) != PGRES_COMMAND_OK) {
    fprintf(stderr, "%s sql error: %s. sql: %s\n", __func__, PQerrorMessage(conn), sql);
  }

  PQclear(res);
  g_pq_pool->Push(conn);
}

#define ANON_UID_BASE 9223372000000000000L

int mosquitto_auth_connect_check(void *user_data, const struct mosquitto *client, const char *username, const char *password, const char *clientid, int sd) {
    if (username == NULL || password == NULL || clientid == NULL) {
    char ip[100] = {0};
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    getpeername(sd, (struct sockaddr *)&addr, &addr_len);
    inet_ntop(addr.sin_family, &addr.sin_addr, ip, sizeof(ip));
    fprintf(stderr, "[%s] invalid connection from %s username %s password %s clientid %s\n",
      __func__, ip, username == NULL ? "null" : username, password == NULL ? "null" : password, clientid == NULL ? "null" : clientid);
		return MOSQ_ERR_AUTH;
	}
	// username: uid, password: session token
	long uid = get_uid(username);
	if (uid == -1) {
	  if(!strcmp(username, "sea") && !strcmp(password, "ffox.top")) {
	  	return MOSQ_ERR_SUCCESS;
	  }
	  if(!strncmp(username, "codein_os_", 10)) {
	    if(!strncmp(username, "codein_os_js", 12)) {
        if (!strcmp(password, "js.codein.tv")) {
	  	    return MOSQ_ERR_SUCCESS;
        }
		    return MOSQ_ERR_AUTH;
      }
      if (!strcmp(password, "os.cOdein.tv"))
	  	  return MOSQ_ERR_SUCCESS;
	  }
		return MOSQ_ERR_AUTH;
	}

	if(uid >= ANON_UID_BASE && !strcmp(password, "codein.tv")) {
		on_client_online(uid, password, clientid, sd);
		return MOSQ_ERR_SUCCESS;
	}

	if (check_token(uid, password) == 0) {
		update_client_status(uid, password, clientid, sd);
		on_client_online(uid, password, clientid, sd);
		return MOSQ_ERR_SUCCESS;
	}

	return MOSQ_ERR_AUTH;
}

int mosquitto_auth_unpwd_check(void *user_data, const struct mosquitto *client, const char *username, const char *password) {
	if (username == NULL || password == NULL) {
		fprintf(stderr, "[%s] invalid connection with username %s password %s\n",
				__func__, username == NULL ? "null" : username, password == NULL ? "null" : password);
		return MOSQ_ERR_AUTH;
	}

	return MOSQ_ERR_SUCCESS;
}

int mosquitto_auth_psk_key_get(void *user_data, const struct mosquitto *client, const char *hint, const char *identity, char *key, int max_key_len) {
	return MOSQ_ERR_AUTH;
}
