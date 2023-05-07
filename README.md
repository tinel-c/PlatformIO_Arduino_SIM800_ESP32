# Example project for Platformio

== Setup ==

The setup used the LiliGo-T-Call-SIM800 hardware: https://github.com/Xinyuan-LilyGO/LilyGo-T-Call-SIM800. 
Version: https://www.aliexpress.com/item/1005002035191360.html?spm=a2g0o.productlist.main.7.6b0cb8e8RJFK34&algo_pvid=19f6ee69-bae2-48cd-9968-f222f1057e26&algo_exp_id=19f6ee69-bae2-48cd-9968-f222f1057e26-3&pdp_npi=3%40dis%21USD%2111.02%2111.02%21%21%21%21%21%40211bf48d16834585493226094d07bc%2112000018490214594%21sea%21RO%21792611780&curPageLogUid=K6dVjOgQ07DH
MQTT client used: http://mqtt-explorer.com/


Just copy passwords.h.example to passwords.h and configure your setup.


== Caveats ==

The software covers also receival for SMS. To use that it uses a branch from the official version of TinyGSM library:
* https://github.com/vshymanskyy/TinyGSM/pull/260/files


== Architecture ==

Base idea is to link the board to a MQTT server and use the connection to send and receive sms through the mqtt protocol.
The following mqtt paths are used:

![Image for the mqtt structure](https://ibb.co/xq9WRdx)

To send a sms:
* set esp32SMS/smsSend/to the number that you are sending the sms to
* set esp32SMS/smsSend/text the text that you want to send it to

All the sms are received into:
* esp32SMS/smsReceive/from the number that sent the sms
* esp32SMS/smsReceive/text the text that was sent 

Both areas have a timestamp attached to them to mark when the sms was sent or received respectively.

esp32SMS/Status is the timestamp when the device went live.


