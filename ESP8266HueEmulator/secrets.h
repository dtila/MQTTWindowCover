

const int OTA_PORT = 8266;
const char * host = "bedroom_cover";
const char * admin_username = "*****";
const char * admin_password = "****";

#define mqtt_command_topic "bedroom/cover/set"
#define mqtt_state_topic "bedroom/cover/state"
#define mqtt_position_topic "bedroom/cover/position"


#define IP_ADDRESS IPAddress(172, 25, 1, 250)
#define IP_GATEWAY IPAddress(172, 25, 1, 1)
#define IP_MASK IPAddress(255, 255, 255, 0)

const char* wifi_ssid = "***";
const char* wifi_password = "****";


/************ WIFI and MQTT INFORMATION (CHANGE THESE FOR YOUR SETUP) ******************/
const char* mqtt_server = "172.25.1.71"; // Enter your MQTT server adderss or IP.
const int mqtt_port = 1883;
const char* mqtt_user = "*****";
const char* mqtt_password = "*****";

