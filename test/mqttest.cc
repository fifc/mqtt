/*
Copyright (c) 2009-2014 Roger Light <roger@atchoo.org>

All rights reserved. This program and the accompanying materials
are made available under the terms of the Eclipse Public License v1.0
and Eclipse Distribution License v1.0 which accompany this distribution.
 
The Eclipse Public License is available at
   http://www.eclipse.org/legal/epl-v10.html
and the Eclipse Distribution License is available at
  http://www.eclipse.org/org/documents/edl-v10.php.
 
Contributors:
   Roger Light - initial implementation and documentation.
*/

// ./mqttest  -b im/sys/send -t im/user/1000000  -h codein.tv -u codein -P codein.tv
// ./mqttest  -t svc/os -b codein/live  -h codein.tv -u codein -P codein.tv

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef WIN32
#include <unistd.h>
#else
#include <process.h>
#include <winsock2.h>
#define snprintf sprintf_s
#endif

#include <mosquitto.h>
#include "client_shared.h"

#include "msg.pb.h"
using namespace msg_proto;

bool process_messages = true;
int msg_count = 0;

static void dump_im_msg(const struct mosquitto_message *msg) {
	if (msg->payloadlen < 10) {
    std::cerr << "message size wrong: " << msg->payloadlen;
		return;
	}

	auto p = (unsigned char *)msg->payload;
	int type = (((unsigned int)p[0])<<8)|((unsigned int)p[1]);
	uint64_t id = (((uint64_t)p[2])<<56)
		|(((uint64_t)p[3])<<48)
		|(((uint64_t)p[4])<<40)
		|(((uint64_t)p[5])<<32)
		|(((uint64_t)p[6])<<24)
		|(((uint64_t)p[7])<<16)
		|(((uint64_t)p[8])<<8)
		|((uint64_t)p[9]);

  auto lmt_type = static_cast<InstantMessageType>(type);
  std::cerr << msg->topic << ": " << InstantMessageType_Name(lmt_type) << " id " << id << " len " << msg->payloadlen << '\n';
	switch(lmt_type) {
		case msg_proto::IM_ADD_CONTACT:
      {
        ImAddContact req;
				if (!req.ParseFromArray(p + 10, msg->payloadlen - 10)) {
          std::cerr << "msg " << type << " decode error!";
				} else {
          std::cerr << "-> " << req.Utf8DebugString() << '\n';
				}
      }
			break;
		case msg_proto::IM_SHARE_SOC_ID:
      {
        ImShareSocId req;
				if (!req.ParseFromArray(p + 10, msg->payloadlen - 10)) {
          std::cerr << "msg " << type << " decode error!";
				} else {
          std::cerr << "-> " << req.ShortDebugString() << '\n';
				}
      }
			break;
		case msg_proto::IM_FEEDBACK_MSG:
      {
        ImFeedbackMsg req;
				if (!req.ParseFromArray(p + 10, msg->payloadlen - 10)) {
          std::cerr << "msg " << type << " decode error!";
				} else {
          std::cerr << "-> " << req.Utf8DebugString() << '\n';
				}
      }
			break;
		default:
      std::cerr << __func__ << "unknown msg type " << type << ", id=" << id << "\n";
			break;
	}	
}
static void dump_mqtt(const struct mosquitto_message *msg) {
	if (strncmp("im/user/", msg->topic, 8) == 0) {
    dump_im_msg(msg);
    return;
  }

	if (strncmp("codein/live", msg->topic, 11) != 0 && strncmp("svc/os", msg->topic, 6) != 0) {
    std::cerr << "ignoring message from topic " << msg->topic << '\n';
		return;
	}
	if (msg->payloadlen < 10) {
    std::cerr << "message size wrong: " << msg->payloadlen;
		return;
	}

	auto p = (unsigned char *)msg->payload;
	int type = (((unsigned int)p[0])<<8)|((unsigned int)p[1]);
	uint64_t id = (((uint64_t)p[2])<<56)
		|(((uint64_t)p[3])<<48)
		|(((uint64_t)p[4])<<40)
		|(((uint64_t)p[5])<<32)
		|(((uint64_t)p[6])<<24)
		|(((uint64_t)p[7])<<16)
		|(((uint64_t)p[8])<<8)
		|((uint64_t)p[9]);

  auto lmt_type = static_cast<LiveMessageType>(type);
  std::cerr << "recv: " << msg->topic << ": " << LiveMessageType_Name(lmt_type) << " id " << id << " len " << msg->payloadlen << '\n';
	switch(lmt_type) {
		case msg_proto::LMT_SYSTEM_NOTIFICATION:
      std::cerr << " id " << id << "\n";
			break;
		case msg_proto::LMT_SERVICE_RESPONSE:
      {
        ServiceResponse req;
				if (!req.ParseFromArray(p + 10, msg->payloadlen - 10)) {
          std::cerr << "msg " << type << " decode error!";
				} else {
          std::cerr << "-> " << req.Utf8DebugString() << '\n';
				}
      }
			break;
		case msg_proto::LMT_NEARBY_USER_UPDATE:
      {
        NearbyUserUpdate req;
				if (!req.ParseFromArray(p + 10, msg->payloadlen - 10)) {
          std::cerr << "msg " << type << " decode error!";
				} else {
          std::cerr << "-> " << req.ShortDebugString() << '\n';
				}
      }
			break;
		case msg_proto::LMT_GAME_CONTACT_INFO:
      {
        GameContactInfo req;
				if (!req.ParseFromArray(p + 10, msg->payloadlen - 10)) {
          std::cerr << "msg " << type << " decode error!";
				} else {
          std::cerr << "-> " << req.Utf8DebugString() << '\n';
				}
      }
			break;
		case msg_proto::LMT_GAME_LIVE_STATE:
			{
				msg_proto::GameLiveStateInfo req;
				if (!req.ParseFromArray(p + 10, msg->payloadlen - 10)) {
          std::cerr << "msg " << type << " decode error!";
				} else {
          std::cerr << "-> " << req.Utf8DebugString() << '\n';
				}
			}
			break;
		case msg_proto::LMT_LIVE_CONNECT_NOTIFY:
			{
				msg_proto::LiveConnectNotify req;
				if (!req.ParseFromArray(p + 10, msg->payloadlen - 10)) {
          std::cerr << "msg " << type << " decode error!";
				} else {
          std::cerr << req.Utf8DebugString() << '\n';
				}
			}
			break;
		case msg_proto::LMT_SERVER_STATUS:
			{
				msg_proto::ServerStatus req;
				if (!req.ParseFromArray(p + 10, msg->payloadlen - 10)) {
          std::cerr << "msg " << type << " decode error!";
				} else {
          std::cerr << req.Utf8DebugString() << '\n';
				}
			}
			break;
		default:
      std::cerr << __func__ << "unknown msg type " << type << ", id=" << id << "\n";
			break;
	}	
}

void my_message_callback(struct mosquitto *mosq, void *obj, const struct mosquitto_message *message)
{
	struct mosq_config *cfg;
	int i;
	bool res;

	if(process_messages == false) return;

	assert(obj);
	cfg = (struct mosq_config *)obj;

	if(message->retain && cfg->no_retain) return;
	if(cfg->filter_outs){
		for(i=0; i<cfg->filter_out_count; i++){
			mosquitto_topic_matches_sub(cfg->filter_outs[i], message->topic, &res);
			if(res) return;
		}
	}

  dump_mqtt(message);

  if(cfg->msg_count>0){
		msg_count++;
		if(cfg->msg_count == msg_count){
			process_messages = false;
			mosquitto_disconnect(mosq);
		}
	}
}

static void send_mqtt_msg(struct mosquitto *mosq, const char *topic,
    unsigned short type, long id, const ::google::protobuf::Message& msg) {
  std::cerr << __func__ << " -> topic: " << topic << " type: " << type << " id " << id << ' ' << msg.DebugString() << '\n';
  int len = msg.ByteSize();
  char buf[len+10];
  msg.SerializeToArray(buf+10, len);
  buf[0] = type>>8;
  buf[1] = type&0xff;
  buf[2] = (id>>56)&0xff;
  buf[3] = (id>>48)&0xff;
  buf[4] = (id>>40)&0xff;
  buf[5] = (id>>32)&0xff;
  buf[6] = (id>>24)&0xff;
  buf[7] = (id>>16)&0xff;
  buf[8] = (id>>8)&0xff;
  buf[9] = id&0xff;
  int mid = 0;
  int rc = mosquitto_publish(mosq, &mid, topic, len+10, buf, 1, 0);
  if (rc == MOSQ_ERR_SUCCESS) {
    fprintf(stderr, "publish success. mid: %d\n", mid);
  } else {
    fprintf(stderr, "publish error: %d\n", rc);
  }
}

void send_live_msg_game_state(struct mosquitto *mosq, const char *topic) {
  auto id = 12345678901234000;
  for (int i = 0; i < 4; ++i, ++id) {
    msg_proto::GameLiveStateInfo msg;
    msg.set_state(msg_proto::game_state_idle);
    msg.set_time(time(NULL)*1000);
    auto clinfo = msg.mutable_client_info();
    clinfo->set_uid(20170009);
    clinfo->set_device_id("mqtt_device_123");
    auto gps = clinfo->mutable_gps();
    gps->set_latitude(301234667);
    gps->set_longitude(1181234667);

    send_mqtt_msg(mosq, topic, msg_proto::LMT_GAME_LIVE_STATE, id, msg);
  }
}

void send_game_contact_msg(struct mosquitto *mosq, const char *topic) {
  auto id = 100000000050000000;
  for (int i = 0; i < 1; ++i, ++id) {
    msg_proto::GameContactInfo msg;
    auto user = msg.mutable_from();
    msg.set_type(msg_proto::GameContactInfo::interested);
    user->set_uid(2017003);
    user = msg.mutable_to();
    user->set_uid(2017004);

    auto gps = msg.mutable_gps();
    gps->set_latitude(22.51325409);
    gps->set_longitude(113.94549413);
    *msg.mutable_gps() = msg.gps();
    send_mqtt_msg(mosq, topic, msg_proto::LMT_GAME_CONTACT_INFO, id, msg);

    /*
    sleep(3);
    msg.set_type(msg_proto::GameContactInfo::invite);
    user->set_uid(2017004);
    user = msg.mutable_from();
    user->set_uid(2017002);
    */

    send_mqtt_msg(mosq, topic, msg_proto::LMT_GAME_CONTACT_INFO, id, msg);
  }
}

void send_msg(struct mosquitto *mosq, const char *topic) {
  auto id = 100000000070000000;
  for (int i = 0; i < 1; ++i, ++id) {
    //msg_proto::ImChatMsg msg;
    //auto user = msg.mutable_from();
    //msg.set_id(time(0));
    //msg.set_body("参加游戏赢大奖！");
    //user->set_uid(1000000);
    //user->set_name("系统");
    //user = msg.mutable_to();
    //user->set_uid(2017002);

    //msg_proto::ServerStatus msg;
    //msg.set_cmd(1);
    SearchNearbyUsers msg;
    auto clinfo = msg.mutable_client_info();
    clinfo->set_uid(20170009);
    clinfo->set_device_id("mqtt_device_123");
    auto gps = clinfo->mutable_gps();
    gps->set_latitude(301234667);
    gps->set_longitude(1181234667);

    send_mqtt_msg(mosq, topic, msg_proto::LMT_SEARCH_NEARBY_USERS, id, msg);
  }
}

int g_connected = false;
void my_connect_callback(struct mosquitto *mosq, void *obj, int result) {
	int i;
	struct mosq_config *cfg;

	assert(obj);
	cfg = (struct mosq_config *)obj;

	if(!result){
    g_connected = true;
		for(i=0; i<cfg->topic_count; i++){
			mosquitto_subscribe(mosq, NULL, cfg->topics[i], cfg->qos);
		}
    send_msg(mosq, cfg->topic);
	}else{
		if(result && !cfg->quiet){
			fprintf(stderr, "%s\n", mosquitto_connack_string(result));
		}
	}
}

void my_subscribe_callback(struct mosquitto *mosq, void *obj, int mid, int qos_count, const int *granted_qos)
{
	int i;
	struct mosq_config *cfg;

	assert(obj);
	cfg = (struct mosq_config *)obj;

	if(!cfg->quiet) printf("Subscribed (mid: %d): %d", mid, granted_qos[0]);
	for(i=1; i<qos_count; i++){
		if(!cfg->quiet) printf(", %d", granted_qos[i]);
	}
	if(!cfg->quiet) printf("\n");
}

void my_log_callback(struct mosquitto *mosq, void *obj, int level, const char *str)
{
	//fprintf(stderr, "[log] %s\n", str);
}

#define VERSION "1.0"

void print_usage(void)
{
	int major, minor, revision;

	mosquitto_lib_version(&major, &minor, &revision);
	printf("sub_test is a simple mqtt client that will subscribe to a single topic and print all messages it receives.\n");
	printf("sub_test version %s running on libmosquitto %d.%d.%d.\n\n", VERSION, major, minor, revision);
	printf("Usage: sub_test [-c] [-h host] [-k keepalive] [-p port] [-q qos] [-R] -t topic ...\n");
	printf("                     [-C msg_count] [-T filter_out]\n");
#ifdef WITH_SRV
	printf("                     [-A bind_address] [-S]\n");
#else
	printf("                     [-A bind_address]\n");
#endif
	printf("                     [-i id] [-I id_prefix]\n");
	printf("                     [-d] [-N] [--quiet] [-v]\n");
	printf("                     [-u username [-P password]]\n");
	printf("                     [--will-topic [--will-payload payload] [--will-qos qos] [--will-retain]]\n");
#ifdef WITH_TLS
	printf("                     [{--cafile file | --capath dir} [--cert file] [--key file]\n");
	printf("                      [--ciphers ciphers] [--insecure]]\n");
#ifdef WITH_TLS_PSK
	printf("                     [--psk hex-key --psk-identity identity [--ciphers ciphers]]\n");
#endif
#endif
#ifdef WITH_SOCKS
	printf("                     [--proxy socks-url]\n");
#endif
	printf("       sub_test --help\n\n");
	printf(" -A : bind the outgoing socket to this host/ip address. Use to control which interface\n");
	printf("      the client communicates over.\n");
	printf(" -c : disable 'clean session' (store subscription and pending messages when client disconnects).\n");
	printf(" -C : disconnect and exit after receiving the 'msg_count' messages.\n");
	printf(" -d : enable debug messages.\n");
	printf(" -h : mqtt host to connect to. Defaults to localhost.\n");
	printf(" -i : id to use for this client. Defaults to sub_test_ appended with the process id.\n");
	printf(" -I : define the client id as id_prefix appended with the process id. Useful for when the\n");
	printf("      broker is using the clientid_prefixes option.\n");
	printf(" -k : keep alive in seconds for this client. Defaults to 60.\n");
	printf(" -N : do not add an end of line character when printing the payload.\n");
	printf(" -p : network port to connect to. Defaults to 1883.\n");
	printf(" -P : provide a password (requires MQTT 3.1 broker)\n");
	printf(" -q : quality of service level to use for the subscription. Defaults to 0.\n");
	printf(" -R : do not print stale messages (those with retain set).\n");
#ifdef WITH_SRV
	printf(" -S : use SRV lookups to determine which host to connect to.\n");
#endif
	printf(" -t : mqtt topic to subscribe to. May be repeated multiple times.\n");
	printf(" -T : topic string to filter out of results. May be repeated.\n");
	printf(" -u : provide a username (requires MQTT 3.1 broker)\n");
	printf(" -v : print published messages verbosely.\n");
	printf(" -V : specify the version of the MQTT protocol to use when connecting.\n");
	printf("      Can be mqttv31 or mqttv311. Defaults to mqttv31.\n");
	printf(" --help : display this message.\n");
	printf(" --quiet : don't print error messages.\n");
	printf(" --will-payload : payload for the client Will, which is sent by the broker in case of\n");
	printf("                  unexpected disconnection. If not given and will-topic is set, a zero\n");
	printf("                  length message will be sent.\n");
	printf(" --will-qos : QoS level for the client Will.\n");
	printf(" --will-retain : if given, make the client Will retained.\n");
	printf(" --will-topic : the topic on which to publish the client Will.\n");
#ifdef WITH_TLS
	printf(" --cafile : path to a file containing trusted CA certificates to enable encrypted\n");
	printf("            certificate based communication.\n");
	printf(" --capath : path to a directory containing trusted CA certificates to enable encrypted\n");
	printf("            communication.\n");
	printf(" --cert : client certificate for authentication, if required by server.\n");
	printf(" --key : client private key for authentication, if required by server.\n");
	printf(" --ciphers : openssl compatible list of TLS ciphers to support.\n");
	printf(" --tls-version : TLS protocol version, can be one of tlsv1.2 tlsv1.1 or tlsv1.\n");
	printf("                 Defaults to tlsv1.2 if available.\n");
	printf(" --insecure : do not check that the server certificate hostname matches the remote\n");
	printf("              hostname. Using this option means that you cannot be sure that the\n");
	printf("              remote host is the server you wish to connect to and so is insecure.\n");
	printf("              Do not use this option in a production environment.\n");
#ifdef WITH_TLS_PSK
	printf(" --psk : pre-shared-key in hexadecimal (no leading 0x) to enable TLS-PSK mode.\n");
	printf(" --psk-identity : client identity string for TLS-PSK mode.\n");
#endif
#endif
#ifdef WITH_SOCKS
	printf(" --proxy : SOCKS5 proxy URL of the form:\n");
	printf("           socks5h://[username[:password]@]hostname[:port]\n");
	printf("           Only \"none\" and \"username\" authentication is supported.\n");
#endif
	printf("\nSee http://mosquitto.org/ for more information.\n\n");
}

static void my_disconnect_callback(struct mosquitto *mosq, void *obj, int rc)
{
	g_connected = 0;
	switch (rc) {
		case MOSQ_ERR_AUTH:
			fprintf(stderr, "MOSQ_ERR_AUTH ");
			break;
		case MOSQ_ERR_CONN_REFUSED:
			fprintf(stderr, "CONN_REFUSED ");
			break;
		default:
			fprintf(stderr, "ERROR %d ", rc);
	} 
	fprintf(stderr, "Disconnected!\n");
}

static int g_last_mid_sent = 0;
static void my_publish_callback(struct mosquitto *mosq, void *obj, int mid)
{
	g_last_mid_sent = mid;
}

int main(int argc, char *argv[])
{
	struct mosq_config cfg;
	struct mosquitto *mosq = NULL;
	int rc;
	
	rc = client_config_load(&cfg, CLIENT_SUB, argc, argv);
	if(rc){
		client_config_cleanup(&cfg);
		if(rc == 2){
			/* --help */
			print_usage();
		}else{
			fprintf(stderr, "\nUse 'mqttest --help' to see usage.\n");
		}
		return 1;
	}

	mosquitto_lib_init();

	if(client_id_generate(&cfg, "code")){
		return 1;
	}

	mosq = mosquitto_new(cfg.id, cfg.clean_session, &cfg);
	if(!mosq){
		switch(errno){
			case ENOMEM:
				if(!cfg.quiet) fprintf(stderr, "Error: Out of memory.\n");
				break;
			case EINVAL:
				if(!cfg.quiet) fprintf(stderr, "Error: Invalid id and/or clean_session.\n");
				break;
		}
		mosquitto_lib_cleanup();
		return 1;
	}
	if(client_opts_set(mosq, &cfg)){
		return 1;
	}

	mosquitto_log_callback_set(mosq, my_log_callback);
	mosquitto_subscribe_callback_set(mosq, my_subscribe_callback);
	mosquitto_connect_callback_set(mosq, my_connect_callback);
	mosquitto_message_callback_set(mosq, my_message_callback);
	mosquitto_publish_callback_set(mosq, my_publish_callback);
	mosquitto_disconnect_callback_set(mosq, my_disconnect_callback);

	rc = client_connect(mosq, &cfg);
	if(rc) return rc;


	rc = mosquitto_loop_forever(mosq, -1, 1);

	mosquitto_destroy(mosq);
	mosquitto_lib_cleanup();

	if(cfg.msg_count>0 && rc == MOSQ_ERR_NO_CONN){
		rc = 0;
	}
	if(rc){
		fprintf(stderr, "Error: %s\n", mosquitto_strerror(rc));
	}
	return rc;
}

