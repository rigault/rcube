let userId = "admin"; // should be "" to enable authent
let password = "admin"; // idem
let DMSType = DMS_DISPLAY.DMS; 
//let userId = "";
//let password = "";
let gpsActivated = false;
let aisActivated = false;
let gpsTimer = 60;                                 // number of second between GPS server requests. 0 desactivates
let aisTimer = 60;                                 // number of second between AIS server requests. 0 desactivates
let rCubeKey = "C2A8tBkbZ9LmTk3JEfj7jSTmmxEYG53q";
let apiUrl = "http://localhost:8080";
//let apiUrl = `https://rcube.ddns.net/post-api/?_=${Date.now()}`;
const clearAppState = false;                       // true to clear all state 
let gpsUrl = "http://localhost:8090/gps";
let aisUrl = "http://localhost:8090/ais";
    
