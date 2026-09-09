#pragma once

// Login form, split around the optional error message so main.cpp can
// insert it conditionally without any template/placeholder logic.

const char LOGIN_HTML_HEAD[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><title>ESP32-CAM Login</title></head>
<body style="font-family:sans-serif;display:flex;justify-content:center;align-items:center;height:100vh;margin:0;background:#111;color:#eee">
<form method="POST" action="/login" style="background:#222;padding:2rem;border-radius:8px;min-width:220px">
<h2 style="margin-top:0">ESP32-CAM Login</h2>
)rawliteral";

const char LOGIN_HTML_ERROR[] PROGMEM =
    R"rawliteral(<p style="color:#f66">Invalid username or password</p>)rawliteral";

const char LOGIN_HTML_FORM[] PROGMEM = R"rawliteral(
<input name="username" placeholder="Username" style="display:block;margin-bottom:1rem;padding:0.5rem;width:100%;box-sizing:border-box">
<input name="password" type="password" placeholder="Password" style="display:block;margin-bottom:1rem;padding:0.5rem;width:100%;box-sizing:border-box">
<button type="submit" style="width:100%;padding:0.5rem">Log In</button>
</form></body></html>
)rawliteral";
