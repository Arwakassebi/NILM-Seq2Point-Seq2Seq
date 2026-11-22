# Firmware ESP32

Le firmware de l'ESP32 (acquisition PZEM-004T → publication MQTT) n'est pas encore versionné dans ce dépôt.
Ajoutez ici votre fichier `.ino` (ou projet PlatformIO) en veillant à **ne pas y laisser** le mot de passe Wi-Fi ni les identifiants MQTT.

## Spécification (d'après le rapport, § 3.3)

| Élément | Valeur |
|---|---|
| Capteur | PZEM-004T v3.0 (Modbus RTU sur RS-485) |
| Adaptateur | MAX485 (TTL ↔ RS-485), contrôle via GPIO 21/22 |
| Période de mesure | 6 s |
| Transport | MQTT (Wi-Fi) vers Mosquitto sur le Raspberry Pi |
| Topic | `home/pzem` |
| Payload | `{"voltage":215.0,"current":5.911,"power":1270.1,"energy":8308,"frequency":50.0,"pf":0.98,"ts":1744016400}` |

Cycle du firmware : initialisation Wi-Fi/MQTT/NTP → vérification MQTT (reconnexion sinon) → requête Modbus → validation CRC (log d'erreur sinon) → décodage + publication → pause 6 s.
