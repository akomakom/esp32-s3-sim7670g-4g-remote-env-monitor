// Template for secrets/ca_cert.h — copy to ca_cert.h and fill in.
// See ca_cert.h header for details.
#pragma once
static const char MQTT_CA_CERT[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
PASTE_YOUR_BROKER_CA_CHAIN_HERE
-----END CERTIFICATE-----
)EOF";
