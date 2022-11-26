#include <Arduino.h>
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <SPI.h>
#include <ArduinoHttpClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <NTPClient.h>
#include "parameters.h"

// Le shiel Ethernet utilise les broches
// 10 : SS pour Ethernet
//  4 : SS pour Carte SD
// 50 : MISO
// 51 : MOSI
// 52 : SCK
// 53 : SS  doit être configuré en output, bien que non utilisé par le shield W5100 sinon l'interface SPI ne foncionne pas
//          d'après la doc Arduino officielle
//

// attention, il faut utiliser la librairie Ethernet modifiée (sinon pas de hostname)
// https://github.com/technofreakz/Ethernet/archive/master.zip


#define IPFIXE    // à commenter pour utiliser le DHCP

// Serial : port USB : affiche les infos sur le fonctionnement
// Serial1 : port RS232 : communique avec le poele RIKA

#define baudUSB 115200
#define baudRIKA 38400


#define port_serveur 10005

// E/S de l'Arduino
#define ouverture_pin 9
#define led_comm      7
#define led_erreur    5
#define led_sac       3     

// PARAMETRES RESEAU ETHERNET
// pour gagner de la place en mémoire, on n'utilisera pas les DNS
// il faut donc fournir l'adresse IP des machines à joindre
//
// Le module va régulièrement envoyer une chaine JSON avec des données (status, erreur ou ordre executé, nombre de sacs)
// http://#IP_HOMEASSISTANT#/api/webhook/#webhook_ID#

//#################### VARIABLES GLOBALES ###########
#define IPFIXE
IPAddress ip(192, 168, 5, 30);
IPAddress dns(192, 168, 5, 1);
IPAddress gateway(192, 168, 5, 1);
IPAddress subnet(255, 255, 255, 0);

byte mac[] = {0xDE,0xAD,0xBE,0xEF,0xFE,0xEF};  // mac address de l'arduino

// Current time
unsigned long currentTime = millis();
// Previous time
unsigned long previousTime = 0; 
// Define timeout time in milliseconds (example: 2000ms = 2s)
const long timeoutTime = 2000;


EthernetClient client;
EthernetServer RIKAserveur = EthernetServer(port_serveur);
// Client NTP pour avoir l'heure
EthernetUDP ntpUDP;
NTPClient temps(ntpUDP, IP_NTP_SERVER, 3600, 60000);




// VARIABLES GLOBALES
char old_porte_status = 0;
volatile char porte_status = 0;

String header;              // pour mémoriser la requete HTTP
String commande_status = "aucune";

String requetePoele= "";
volatile boolean requetePoeleComplete = false;
String requeteUSB="";
volatile boolean requeteUSBComplete = false;
String dataHTTP="";
String POELE_STATUS="STATUS INCONNU";

bool old_b_status = 1;   // status du bouton de la trappe à granulés 1= fermé, 0= ouvert
long chrono_start=0;
long chrono_stop=0;
long duree_ouverture=0;
unsigned char sacs_verses=0;

bool errorhttp = false;
unsigned char erreur=0;     // numero de l'erreur en cas de commande reçue via HTTP
String sms="NONE";
String last_sms="NONE";
String STATUS="AUCUN STATUS";
const String numtel="+33123456789";
const String codepin="2107";
String jour="70/01/01";
String heure="01:00:00";
char recu;



void clignote(unsigned char led, unsigned char repete, unsigned int delay_on, unsigned int delay_off) {
    unsigned char i;
    for (i=0;i<repete; i++) {
        digitalWrite(led,HIGH);
        delay(delay_on);
        digitalWrite(led,LOW);
        delay(delay_off);
    }
}



void send_retour(void) {     // on envoie CR + LF au poele
    Serial1.write(char(13));
    Serial1.write(char(10));
}
void send_OK(void) {        // on envoie OK au poele
    Serial1.print("OK");
    Serial.println("-> Reponse : OK");
    Serial.println();
    send_retour();
}
void send_ERROR(void) {
    Serial1.print("ERROR");
    Serial.println("-> Réponse : ERROR");
    Serial.println();
    send_retour();
}

void get_date(void) {
	  Serial.println("--> Demande de date/heure sur serveur NTP ");
    temps.update();
    unsigned long epochTime = temps.getEpochTime();
    //Get a time structure
    struct tm *ptm = gmtime ((time_t *)&epochTime);
    int jour_mois = ptm->tm_mday;
    int mois = ptm->tm_mon+1;
    int annee = ptm->tm_year+1900-30-2000;  // -30 pour une raison inconnue , format AA et non AAAA
    jour= String(annee) +"/"+ String(mois) +"/"+ String(jour_mois);
 
    Serial.print("----> Jour : ");
    Serial.println(jour);
    heure=temps.getFormattedTime();
    Serial.print("----> Heure : ");
    Serial.println(heure);
}
////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
/*
 SerialEvent occurs whenever a new data comes in the
 hardware serial RX.  This routine is run between each
 time loop() runs, so using delay inside loop can delay
 response.  Multiple bytes of data may be available.
 */
void serialEvent1() {
  while (Serial1.available()) {
    // get the new byte:
    char inChar = (char)Serial1.read();
    // add it to the inputString:
    requetePoele += inChar;
    // if the incoming character is a newline, set a flag
    // so the main loop can do something about it:
    if ((inChar == '\n') || (inChar == char(26)) || (inChar == char(13))) {
      requetePoeleComplete = true;
    }
  }
}


void serialEvent() {
  while (Serial.available()) {
    // get the new byte:
    char inChar = (char)Serial.read();
    // add it to the inputString:
    requeteUSB += inChar;
    // if the incoming character is a newline, set a flag
    // so the main loop can do something about it:
    if (inChar == '\n') {
      requeteUSBComplete = true;
    }
  }
}

bool isDIGIT(String chaine) {  // verifie qu'une chaine ne contient que des nombres
  bool reponse = true;
  for (unsigned int i=0;i<chaine.length();i++) {
     reponse = reponse & isDigit(chaine[i]);
  }
  return reponse;
}


int send_data(unsigned int ouverture, String commande, String poele_status) {
  int httpResponseCode = -1;
  String url = "/api/webhook/";
    url += webhook;

  Serial.print("--> URL = ");
  Serial.println(url);

  
  HttpClient http(client, ha_ip, ha_port);
  StaticJsonDocument<200> data;
  String data_JSON;
  String content_type = "application/json";
  if (ouverture==1) {data["Porte"] = 0;}
  if (ouverture==0) {data["Porte"] = 1;}     // pour avoir une logique conforme aux attentes de HomeAssistant
  data["Commande"] = commande;
  data["Status"] = poele_status;
  Serial.print("--> JSON = ");
  serializeJson(data, Serial);
  Serial.println();
  serializeJson(data, data_JSON);
  httpResponseCode = http.post(url, content_type, data_JSON);
  if (httpResponseCode == 200) { //Check for the returning code
    Serial.print("--> Réponse HomeAssistant: ");
    Serial.print(httpResponseCode);
    Serial.println(" --> OK");
  }
  else {
    Serial.print("--> Erreur de requête HTTP POST : ");
    Serial.println(httpResponseCode);
  }
  http.stop(); 
  return httpResponseCode;
}


void setup() {
  // On prépare le port série
  Serial.begin(baudUSB);
  Serial1.begin(baudRIKA);
  Serial.println();
  Serial.println("Démarrage simulateur modem Rika V2_HA ...");
  requetePoele.reserve(254);
  dataHTTP.reserve(512);
  requeteUSB.reserve(10);
  sms.reserve(100);
  // On prépare les Entrées/Sorties
  Serial.print("-> préparation E/S : ");
  pinMode(53, OUTPUT);          // nécessaire pour faire fonctionner le shield Ethernet W5100 sur carte Mega1280 (ou 2560)
  pinMode(4, OUTPUT);           // nécessaire pour désactiver la carte SD du shield Ethernet et activer Ethernet
  digitalWrite(4,HIGH);         // nécessaire pour désactiver la carte SD du shield Ethernet et activer Ethernet
  //pinMode(10, OUTPUT);           // nécessaire pour désactiver le port Ethernet et activer la carte SD
  //digitalWrite(10,HIGH);         // nécessaire pour désactiver le port Ethernet et activer la carte SD
  pinMode(led_comm, OUTPUT);
  pinMode(led_erreur, OUTPUT);
  pinMode(led_sac, OUTPUT);
  pinMode(ouverture_pin, INPUT_PULLUP);
  // on met les bons niveaux sur les sorties
  digitalWrite(led_sac,LOW);
  digitalWrite(led_comm,LOW);
  digitalWrite(led_erreur,LOW);
  Serial.println("OK");
  clignote(led_comm,2,100,200);
  clignote(led_sac,2,100,200);
  clignote(led_erreur,2,100,200);

  // affichage des infos réseau
  Serial.print("-> adresse IP : ");
#ifdef IPFIXE 
  Ethernet.begin(mac,ip,dns,gateway,subnet);
  Serial.print(" IP FIXE : ");  
  delay(300);     
#else
  if (!Ethernet.begin(mac)) {                                   // adresse IP obtenue par DHCP
    Serial.print(" ERREUR DHCP : impossible de continuer !");       
    while(1) {
      clignote(led_erreur,3,100,250);
      delay(500); 
    }
  }
#endif
  //Ethernet.hostName("RIKA");
  Serial.println(Ethernet.localIP());
  Serial.print("-> Masque : ");
  Serial.println(Ethernet.subnetMask());
  Serial.print("-> Passerelle : ");
  Serial.println(Ethernet.gatewayIP());
  Serial.print("-> DNS : ");
  Serial.println(Ethernet.dnsServerIP());
  //Serial.print("-> Hostname : ");
  //Serial.println(Ethernet.getHostName());
  clignote(led_erreur, 2,100,200);
  delay(1000);
  Serial.print("-> serveur sur port ");
  Serial.print(port_serveur);
  Serial.print(" : ");
  RIKAserveur.begin();
  Serial.println("OK");
  Serial.println("-> Système prêt !");
  Serial.println();
  clignote(led_comm,2,100,200);
  clignote(led_sac,2,100,200);
  clignote(led_erreur,2,100,200);
//fin de la boucle setup()
}

void loop() {
  // la porte a-t-elle été ouverte ?
  porte_status = digitalRead(ouverture_pin);
  if (porte_status != old_porte_status) {
    if (porte_status) {Serial.println("----> Porte ouverte");} else {Serial.println("----> Porte fermée");}
    send_data(porte_status, commande_status,POELE_STATUS);
    old_porte_status = porte_status;
  }
    
  //A-t-on reçu une requete venant du poele ?
  if (requetePoeleComplete) {
    Serial.print("Reçu : ");
    Serial.print(requetePoele);
    digitalWrite(led_comm,HIGH);
    delay(100);
    digitalWrite(led_comm,LOW);
    // Il reste maintenant à traiter cette requete
    if (requetePoele.startsWith("AT+CMGS")) {      // le poele veut envoyer un SMS
      // on donne l'invite >
      send_retour();
      Serial1.write(">");
      //Affichage
      Serial.write("-> Envoi SMS ");
      Serial.write("-> Message : ");
      // on récupère le contenu du SMS
      delay(2000); // delai pour laisser un peu de temps au poele pour répondre
      STATUS="";
      recu=0;
      while (recu !=char(26)) {
        if (Serial1.available()) {
          recu = (char)Serial1.read();
          if (recu != char(26)) {   // ctrl+z (ASCII 26) pour finir le SMS
            STATUS+=recu;
          }
        }
      }
      //Affichage
      Serial.println(STATUS);
      Serial.println("-> +CMGS : 01");
      //reponse
      send_retour();
      Serial1.print("+CMGS : 01");
      send_retour();
      send_OK();
      // on envoie les données à HomeAssistant
      POELE_STATUS = STATUS;
      send_data(porte_status, commande_status,POELE_STATUS);
    }
    else if (requetePoele.startsWith("AT+CMGR")) {      // le poele veut lire un SMS
      //affichage
      Serial.print("-> Lecture SMS ");
      Serial.print("-> Message : ");
      Serial.println(sms);
      if (sms != "NONE") {
        //Serial.println();
       	get_date(); // fausse requete pour obtenir la date réelle
        Serial.print("-> +CMGR: \"REC READ\",\"");
        Serial.print(numtel);
        Serial.print("\",,\"");
        Serial.print(jour);
        Serial.print(",");
        Serial.print(heure);
        Serial.println("+08\"");
        Serial.print("-> SMS réel : ");
        Serial.print(codepin);
        Serial.print(" ");
        Serial.println(sms);
        // message pour le poele
        send_retour();
        Serial1.print("+CMGR: \"REC READ\",\"");
        Serial1.print(numtel);
        Serial1.print("\",,\"");
        Serial1.print(jour);
        Serial1.print(",");
        Serial1.print(heure);
        Serial1.print("+08\"");
        send_retour();
        Serial1.print(codepin);
        Serial1.print(" ");
        Serial1.print(sms);
        send_retour();
        send_retour();
        send_OK();
      } else {                  // le SMS est none : on n'a aucune commande à transmettre
        send_retour();
        send_OK();
      }
    } else if (requetePoele.startsWith("AT+CMGD")) {      // le poele veut efface les SMS
      last_sms=sms;
      sms="NONE";
      Serial.print("-> effacement SMS  ");
      Serial.print("-> Message : ");
      Serial.println(sms);
      send_OK();
    } else if (requetePoele.startsWith("ATE0") or requetePoele.startsWith("AT+CNMI")  or requetePoele.startsWith("AT+CMGF") ) {  // requete de paramètrage : on répond OK sans poser de questions
      send_OK();
    } else if (requetePoele!="" && requetePoele!="\n" && requetePoele!="\x1A" && requetePoele!= "\x0D" ) {
      send_ERROR();
      digitalWrite(led_erreur,HIGH);
      delay(500);
      digitalWrite(led_erreur,LOW);
    }
    // on remet tout à zéro pour la prochaine requete
    requetePoele= "";
    requetePoeleComplete = false;
  }

  // A-t-on reçu une requete http sur le serveur ?
  client = RIKAserveur.available();
  if (client) {                             // If a new client connects,
    currentTime = millis();
    previousTime = currentTime;
    Serial.println("--> Réception requête HTTP :");          // print a message out in the serial port
    String currentLine = "";                // make a String to hold incoming data from the client
    while (client.connected() && currentTime - previousTime <= timeoutTime) {  // loop while the client's connected
      currentTime = millis();
      if (client.available()) {             // if there's bytes to read from the client,
        char c = client.read();             // read a byte, then
        Serial.write(c);                    // print it out the serial monitor
        header += c;
        if (c == '\n') {                    // if the byte is a newline character
          // if the current line is blank, you got two newline characters in a row.
          // that's the end of the client HTTP request, so send a response:
          if (currentLine.length() == 0) {
            // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
            // and a content-type so the client knows what's coming, then a blank line:
            client.println("HTTP/1.1 200 OK");
            client.println("Content-type:application/json");
            client.println("Connection: close");
            client.println();
            
            // Interprétation et réponse JSON
           errorhttp = false;
            if (header.indexOf("GET /status") >= 0) {
              client.println("{\"commande\":\"status\"}");
              commande_status="status";                       //cette commande n'existe pas pour le poele, mais va forcer à transmettre les donnees à HomeAssistant
              Serial.println("----> Demande STATUS");
            } else if (header.indexOf("GET /off") >= 0) {
              client.println("{\"commande\":\"off\"}");
              commande_status="off";
              Serial.println("----> Demande OFF");
            } else if (header.indexOf("GET /room") >= 0) {
              client.println("{\"commande\":\"room\"}");
              commande_status="room";
              Serial.println("----> Demande ROOM");
            } else if (header.indexOf("GET /heat") >= 0) {
              client.println("{\"commande\":\"heat\"}");
              commande_status="heat";
              Serial.println("----> Demande HEAT");
            } else if (header.indexOf("GET /auto") >= 0) {
              client.println("{\"commande\":\"auto\"}");
              commande_status="auto";
              Serial.println("----> Demande AUTO");
            } else if (header.startsWith("GET /r")) {
              header.remove(0,6);  //suppression du début /GET
              header.remove(2,header.length()-2);     //suppression de la fin HTTP/1.1 etc....
              //analyse des chiffres dans la commande
              String nombre=header;
              if (isDIGIT(nombre)) {
                int room_value=nombre.toInt();
                //Serial.println(valeur);
                if (room_value < 10) {room_value=10;}
                if (room_value > 28) {room_value=28;}
                //Serial.println(valeur);
                header=String(room_value, DEC);
                client.println("{\"commande\":\"r" + header +"\"}");
                commande_status="r" + header ;
                Serial.print("----> Demande :r");
                Serial.println(header);
              } else {
                client.println("{\"commande\":\"erreur\"}");
                commande_status="\"erreur\"";
                Serial.println("--> Erreur : demande non interprétée");
                errorhttp=true;
              }
            } else if (header.startsWith("GET /h")) {
              header.remove(0,6);  //suppression du début /GET
              header.remove(2,header.length()-2);     //suppression de la fin HTTP/1.1 etc....
              //analyse des chiffres dans la commande
              String nombre=header;
              if (isDIGIT(nombre)) {
                int heat_value=nombre.toInt();
                //Serial.println(valeur);
                if (heat_value==10) {heat_value=100;} // comme on n'analyse que les 2 premiers chiffres du nombre, le 100 devient un 10, donc on le remet en 100
                if (heat_value < 30) {heat_value=30;}
                if (heat_value > 100) {heat_value=100;}
                heat_value=heat_value/5; // pour arrondir de 5 en 5
                heat_value=heat_value*5;
                header=String(heat_value, DEC);
                client.println("{\"commande\":\"h" + header +"\"}");
                commande_status="h" + header;
                Serial.print("----> Demande :h");
                Serial.println(header);
                //Serial.println(valeur);
              } else {
                client.println("{\"commande\":\"erreur\"}");
                commande_status="\"erreur\"";
               Serial.println("--> Erreur : demande non interprétée");
               errorhttp=true;
              }
            } else {
              client.println("{\"commande\":\"erreur\"}");
              commande_status="\"erreur\"";
              Serial.println("--> Erreur : demande non interprétée");
              errorhttp=true;
            }

            //on envoie les infos à jour à HomeAssistant
            send_data(porte_status, commande_status, POELE_STATUS);
            //commande_status="aucune";
            // The HTTP response ends with another blank line
            client.println();
            // Break out of the while loop
            break;
          } else { // if you got a newline, then clear currentLine
            currentLine = "";
          }
        } else if (c != '\r') {  // if you got anything else but a carriage return character,
          currentLine += c;      // add it to the end of the currentLine
        }
      }
    }
    // Clear the header variable
    header = "";
    // Close the connection
    client.stop();
    Serial.println("--> Deconnexion Client HTTP ...");
    Serial.println("");
    // on passe la commande au poele
    if (! errorhttp ) {
      if (commande_status!="status") {
        Serial.print("--> SMS transmis au poele : ");
        sms=commande_status;
        Serial.println(sms);
      } else {
        //sms="?";
        send_data(porte_status, commande_status,POELE_STATUS);
      }  
    } else {
      Serial.println("--> Commande non conforme : aucun SMS transmis au poele !");
    }
  }




    // A-t-on reçu une requete via le port USB ?
    if (requeteUSBComplete) {
        digitalWrite(led_comm,HIGH);
        delay(100);
        digitalWrite(led_comm,LOW);
        if (requeteUSB.startsWith("IP")) {
            Serial.print("L'adresse IP est : ");
            Serial.println(Ethernet.localIP());
            Serial.println();
        }
        else if (requeteUSB.startsWith("SMS")) {
            Serial.println("Le dernier SMS envoyé au poele est :");
            Serial.println(last_sms);
            Serial.println();
        }
        else if (requeteUSB.startsWith("STATUS")) {
            Serial.println("Le dernier STATUS reçu du poele est :");
            Serial.println(STATUS);
            Serial.println();
        }
        else
        {
            Serial.println("Menu :");
            Serial.println("IP  -> affiche l'adresse IP");
            Serial.println("SMS -> affiche le dernier SMS envoyé ou reçu");
            Serial.println();
            digitalWrite(led_erreur,HIGH);
            delay(500);
            digitalWrite(led_erreur,LOW);
        }

        // on remet le buffer à zéro
        requeteUSBComplete=false;
        requeteUSB="";
    }


// fin de la boucle loop()
}
