# RIKA_HOMEASSISTANT
Arduino emulates an GSM modem, but works via IP and HTTP requests.

HTTP requests are GET requests and follow this template :
http://ARDUINO_IP:10005/order

order can be
- auto
- off
- room
- heat
- rxx  with xx an integer from 7 to 28
- hyy  with yy an interger from 30 to 100 

Each of these orders corresponds to a functionning mode of Rika 
---------------------------------------
On events (order received), door opened, Arduino sends a JSON to homeassistant

ex : {"Porte":0,"Commande":"h30,"Status":"STOVE - ON : auto"}


It's impossible to have the poele status without sending an order before.

`![alt text](rika.jpg)
