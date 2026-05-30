/**
 * Configuration file for AI Stethoscope Dashboard
 * Update these values to connect to your MQTT broker and topics
 */

// MQTT Broker Configuration
// Set to an empty string for browser prompt, or provide a WebSocket URL
// Examples:
//   'ws://localhost:8080'           (local broker via docker-compose)
//   'ws://your-mqtt-broker.com:8080' (remote broker with WebSocket)
window.MQTT_BROKER = 'ws://localhost:8080';

// MQTT Topics (must match firmware/bridge topics)
window.AUDIO_TOPIC = 'VIAM-AI-STETH/AUDIO_STREAM';
window.ANALYSE_TOPIC = 'VIAM-AI-STETH/ANALYSE';
window.COMMAND_TOPIC = 'VIAM-AI-STETH/COMMAND';
