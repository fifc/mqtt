#!/usr/bin/python3.6

import os, sys, inspect

#pkg_path="/usr/local/lib/python3.6/dist-packages"
#if pkg_path not in sys.path:
#    sys.path.insert(0, pkg_path)

import paho.mqtt.client as mqtt
import time
import base64
import hmac
import urllib.parse

print(mqtt)

deviceId = "py3_121_sdf_009"
hubAddress = "gw.codein.net"

user = "codein"
password = "codein.tv"

pubTopic = 'devices/' + deviceId + '/messages/events/'
subTopic = 'devices/' + deviceId + '/messages/devicebound/#'


def on_connect(client, userdata, flags, rc):
    print("Connected with result code "+str(rc))
    client.subscribe(subTopic)
    msg = encode_msg(1, 261, bytearray(b"testmessage"))
    client.publish(pubTopic, msg)


def on_message(client, userdata, msg):
    print("{0} - {1} ".format(msg.topic, str(msg.payload)))


def encode_msg(msg_type, msg_id, payload):
    msg = bytearray()
    msg += msg_type.to_bytes(2, byteorder='big', signed=False)
    msg += msg_id.to_bytes(8, byteorder='big', signed=False)
    msg += payload
    print(msg)
    return msg

client = mqtt.Client(client_id=deviceId, protocol=mqtt.MQTTv311)
client.on_connect = on_connect
client.on_message = on_message


#client.tls_set("WS_CA1_NEW.crt")
client.username_pw_set(username=user, password=password)
client.connect("gw.codein.net", port=1883, keepalive=60)

client.loop_forever()
