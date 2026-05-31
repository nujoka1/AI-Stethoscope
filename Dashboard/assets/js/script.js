// Minimal dashboard script: show config values and provide a placeholder for live updates
(function(){
  const brokerEl = document.getElementById('broker');
  const audioEl = document.getElementById('audioTopic');
  const analyseEl = document.getElementById('analyseTopic');
  const lastEl = document.getElementById('last');

  // Read config provided by assets/js/config.js
  try{
    brokerEl.textContent = window.MQTT_BROKER || 'not configured';
    audioEl.textContent = window.AUDIO_TOPIC || 'not configured';
    analyseEl.textContent = window.ANALYSE_TOPIC || 'not configured';
  }catch(e){
    brokerEl.textContent = 'config missing';
  }

  // Placeholder: if you later add MQTT over WebSocket, init subscription here
  lastEl.textContent = 'Dashboard restored. Connect a broker to receive live data.';
})();
