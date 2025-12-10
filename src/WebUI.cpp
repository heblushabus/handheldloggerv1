#include "WebUI.h"
#include "SharedData.h"

// Helper to list files
// Helper to list files

const char *html_head = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Air Quality Logger</title>
    <style>
        :root { --bg: #121212; --card: #1e1e1e; --text: #e0e0e0; --accent: #bb86fc; --danger: #cf6679; }
        body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif; background: var(--bg); color: var(--text); margin: 0; padding: 20px; text-align: center; }
        h1 { color: var(--accent); margin-bottom: 10px; }
        .card { background: var(--card); border-radius: 12px; padding: 20px; margin: 10px auto; max-width: 400px; box-shadow: 0 4px 6px rgba(0,0,0,0.3); }
        .grid { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; }
        .stat { display: flex; flex-direction: column; align-items: center; }
        .label { font-size: 0.8rem; opacity: 0.7; }
        .value { font-size: 1.5rem; font-weight: bold; }
        button { background: var(--accent); color: #000; border: none; padding: 12px 20px; border-radius: 8px; font-size: 1rem; cursor: pointer; width: 100%; margin-top: 10px; font-weight: bold; transition: opacity 0.2s; }
        button:hover { opacity: 0.9; }
        button.danger { background: var(--danger); color: #fff; }
        .btn-sm { padding: 5px 10px; font-size: 0.8rem; width: auto; margin: 0 2px; }
        .file-row { display: flex; justify-content: space-between; align-items: center; padding: 8px 0; border-bottom: 1px solid #333; }
        .fname { font-size: 0.9rem; }
        .actions { display: flex; }
        .footer { margin-top: 20px; font-size: 0.8rem; opacity: 0.5; }
    </style>
</head>
<body>
)rawliteral";

const char *html_foot = R"rawliteral(
    <script>
        function update() {
            fetch('/api/data').then(r => r.json()).then(d => {
                document.getElementById('iaq').innerText = d.iaq.toFixed(0);
                document.getElementById('co2').innerText = d.co2.toFixed(0);
                document.getElementById('temp').innerText = d.temp.toFixed(1);
                document.getElementById('hum').innerText = d.hum.toFixed(1);
                document.getElementById('press').innerText = d.press.toFixed(0);
                document.getElementById('volt').innerText = d.volt.toFixed(2);
                document.getElementById('acc').innerText = d.acc;
                document.getElementById('uptime').innerText = Math.floor(d.uptime/1000) + 's';
                document.getElementById('recStatus').innerText = d.isRec ? "RECORDING (" + d.recFile + ")" : "IDLE";
                document.getElementById('recStatus').style.color = d.isRec ? "#cf6679" : "#e0e0e0";
            });
        }
        function del(file) {
            if(confirm('Delete ' + file + '?')) location.href='/delete?file=' + file;
        }
        setInterval(update, 2000);
        update();
    </script>
</body>
</html>
)rawliteral";

void setupWebUI(WebServer &server)
{
    server.on("/", HTTP_GET, [&server]()
              {
                  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
                  server.send(200, "text/html", "");
                  server.sendContent(html_head);
                  server.sendContent("<h1>Air Monitor</h1>");

                  // Live Data Card
                  server.sendContent(F("<div class='card'>"));
                  server.sendContent(F("<div style='margin-bottom:10px; font-weight:bold;' id='recStatus'>Checking...</div>"));
                  server.sendContent(F("<div class='grid'>"));
                  server.sendContent(F("<div class='stat'><span class='label'>IAQ</span><span class='value' id='iaq'>--</span></div>"));
                  server.sendContent(F("<div class='stat'><span class='label'>CO2</span><span class='value' id='co2'>--</span>ppm</div>"));
                  server.sendContent(F("<div class='stat'><span class='label'>Temp</span><span class='value' id='temp'>--</span>°C</div>"));
                  server.sendContent(F("<div class='stat'><span class='label'>Humidity</span><span class='value' id='hum'>--</span>%</div>"));
                  server.sendContent(F("<div class='stat'><span class='label'>Pressure</span><span class='value' id='press'>--</span>Pa</div>"));
                  server.sendContent(F("<div class='stat'><span class='label'>Voltage</span><span class='value' id='volt'>--</span>V</div>"));
                  server.sendContent(F("</div>"));
                  server.sendContent(F("<div style='margin-top:10px; font-size:0.9rem;'>Accuracy: <span id='acc'>0</span>/3</div>"));
                  server.sendContent(F("</div>"));

                  // File List Card
                  server.sendContent("<div class='card'><h3>Recordings</h3>");

                  File root = LittleFS.open("/");
                  if (!root || !root.isDirectory())
                  {
                      server.sendContent("<div>Failed to open dir</div>");
                  }
                  else
                  {
                      File file = root.openNextFile();
                      bool found = false;
                      while (file)
                      {
                          String name = String(file.name());
                          // Check if it's a log file (relaxed check for leading slash)
                          if (name.endsWith(".csv"))
                          { // Added filter for CSV to be cleaner
                              found = true;

                              char buf[384];
                              snprintf(buf, sizeof(buf),
                                       "<div class='file-row'><span class='fname'>%s (%lu b)</span>"
                                       "<div class='actions'><a href='%s' download><button class='btn-sm'>DL</button></a>"
                                       "<button class='btn-sm danger' onclick=\"del('%s')\">X</button></div></div>",
                                       name.c_str(), (unsigned long)file.size(), name.c_str(), name.c_str());
                              server.sendContent(buf);
                          }
                          file = root.openNextFile();
                      }
                      if (!found)
                      {
                          server.sendContent("<div style='opacity:0.6; padding:10px;'>No recordings found.</div>");
                      }
                  }
                  server.sendContent("</div>");

                  server.sendContent("<div class='footer'>Uptime: <span id='uptime'>--</span></div>");
                  server.sendContent(html_foot);
                  server.sendContent(""); // Terminate chunked transfer
              });

    server.on("/api/data", HTTP_GET, [&server]()
              {
        char json[512];
        snprintf(json, sizeof(json), 
            "{\"iaq\":%.2f,\"co2\":%.2f,\"temp\":%.2f,\"hum\":%.2f,\"press\":%.2f,\"volt\":%.2f,\"acc\":%d,\"uptime\":%lu,\"isRec\":%s,\"recFile\":\"%s\"}",
            currentData.iaq,
            currentData.co2,
            currentData.temp,
            currentData.hum,
            currentData.press,
            currentData.voltage,
            currentData.accuracy,
            millis(),
            isRecording ? "true" : "false",
            currentLogFileName
        );
        server.send(200, "application/json", json); });

    // Handle file downloads dynamically
    server.onNotFound([&server]()
                      {
        String path = server.uri();
        if (path.endsWith(".csv") && LittleFS.exists(path)) {
            File file = LittleFS.open(path, "r");
            server.streamFile(file, "text/csv");
            file.close();
        } else {
            server.send(404, "text/plain", "File not found");
        } });

    server.on("/delete", HTTP_GET, [&server]()
              {
        if (server.hasArg("file")) {
            String file = server.arg("file");
            if (LittleFS.exists(file)) {
                LittleFS.remove(file);
            } else if (LittleFS.exists("/" + file)) {
                LittleFS.remove("/" + file);
            }
        }
        server.sendHeader("Location", "/");
        server.send(303); });

    server.begin();
}

void stopWebUI(WebServer &server)
{
    server.stop();
}
