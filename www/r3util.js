/**
 * Set DMS Degree Minute Second Display style
 * update global DMSType variable
 */
function updateDMS() {
  Swal.fire({
    title: "DMS display choice",
    confirmButtonText: "Confirm",
    showCancelButton: true,
    focusConfirm: false,
    input: 'select',
    inputOptions:  ['BASIC', 'DD', 'DM', 'DMS'],
    inputPlaceholder: 'DMS Type'
  }).then((result) => {
    if (result.isConfirmed) {
      console.log("DMS display set to:", result.value);
      DMSType = Number (result.value);
    }
  });
}

/**
 * Re-init one server
 */
function initServer () {
   const formData = `type=${REQ.INIT}`;
   console.log ("Request sent:", formData);
   Swal.fire({
      title: 'Reinit server',
      html: 'It may take time...',
      allowOutsideClick: false,
      didOpen: () => {
         Swal.showLoading();
      }
   });

   fetch (apiUrl, {
      method: "POST",
      headers: {
         "Content-Type": "application/x-www-form-urlencoded"
      },
      body: formData
   })
   .then(response => response.json())
   .then (data => Swal.fire ("Reinit done", `Server: ${data.serverPort}`, "info"));
}

/**
 * display information about point (lat, lon)
 */
function coordStatus (lat, lon) {
   const name = "bidon";
   const formData = `type=${REQ.COORD}&boat=${name},${lat},${lon}&grib=grib/${gribLimits.name}`;
   console.log ("Request sent:", formData);
   Swal.fire({
      title: 'Info about coordinates…',
      html: 'It may take time...',
      allowOutsideClick: false,
      didOpen: () => {
         Swal.showLoading();
      }
   });

   fetch (apiUrl, {
      method: "POST",
      headers: {
         "Content-Type": "application/x-www-form-urlencoded"
      },
      body: formData
   })
   .then(response => response.json())
   .then(data => {
      console.log ("JSON received:", data);
      meteoGram (lat, lon, data);
   })
   .catch (error => {
      console.error("Error statusCoord:", error);
      Swal.fire("Erreur", "Impossible to access server", "error");
   });
}

// true wind direction (deg), same logic as C version
function fTwd(u, v) {
   const val = 180 + RAD_TO_DEG * Math.atan2(u, v);
   return (val > 180) ? (val - 360) : val;
}

// true wind speed (kt), Pythagoras, same as C version
function fTws(u, v) {
   return MS_TO_KN * Math.hypot(u, v);
}

// Map TWD (deg) to a small arrow character for display at top of chart
function directionToArrow(dirDeg) {
   if (!Number.isFinite(dirDeg)) return '•';

   let d = dirDeg;
   if (d < 0) d += 360;
   d = d % 360;

   // 45° sectors
   if (d >= 337.5 || d < 22.5) return '↓';
   if (d < 67.5) return '↙';
   if (d < 112.5) return '←';
   if (d < 157.5) return '↖';
   if (d < 202.5) return '↑';
   if (d < 247.5) return '↗';
   if (d < 292.5) return '→';
   return '↘';
}

// Simple formatter for booleans in footer
function boolToYesNo(b) {
   return b ? 'yes' : 'no';
}

/**
 * Display a meteogram with wind, gust and waves using SweetAlert2 and Plotly.
 *
 * @param {number} lat - Latitude in degrees of the queried point.
 * @param {number} lon - Longitude in degrees of the queried point.
 * @param {Object} data - Meteo data object returned by the server.
 * @param {boolean} data.isSea - True if point is sea.
 * @param {boolean} data.isSeaTolerant - True if point is sea tolerant.
 * @param {boolean} data.inWind - True if point is inside wind domain.
 * @param {boolean} data.inCurrent - True if point is inside current domain.
 * @param {number} data.epochGribStart - Epoch time (seconds) of first row.
 * @param {Array<Array<number>>} data.meteoArray - Rows of [u, v, g, w].
 */
function meteoGram(lat, lon, data) {
   if (!data || !Array.isArray(data.meteoArray) || data.meteoArray.length === 0) {
      Swal.fire('No meteo data', 'meteoArray is empty or missing.', 'error');
      return;
   }
   if (data.inWind == false) {
      let text = `Sea: ${boolToYesNo(data.isSea)}<br>Sea tolerant: ${boolToYesNo(data.isSeaTolerant)}<br>`;
      text += `Current: ${boolToYesNo(data.inCurrent)}`;
      Swal.fire ('No wind', text, 'warning');
      return;
   }

   const meteo = data.meteoArray;
   const epochStart = data.epochGribStart;

   const xDates = [];       // Date objects for x-axis
   const twsArray = [];     // wind speed kt
   const gustArray = [];    // gust kt
   const waveArray = [];    // wave height m
   const arrowText = [];    // wind direction arrows
   const twdDirArray = [];  // wind direction 0-360, integer degrees

   let maxValue = 0;

   for (let i = 0; i < meteo.length; i++) {
      const [u, v, g, w] = meteo [i];

      // Time: epochStart + i * 3600 seconds
      const date = new Date((epochStart + i * 3600) * 1000);
      xDates.push(date);

      // Wind direction and speed
      const twd = fTwd(u, v);        // deg, may be negative
      let twd360 = twd;
      if (twd360 < 0) twd360 += 360;
      twd360 = Math.round(twd360);   // integer degrees 0-360

      const tws = fTws(u, v);        // kt
      const gust = Math.max(tws, MS_TO_KN * g); // gust in kt

      twsArray.push(tws);
      gustArray.push(gust);
      waveArray.push(w);
      twdDirArray.push(twd360);

      maxValue = Math.max(maxValue, tws, gust, w);

      arrowText.push(directionToArrow(twd));
   }

   // y-position for direction arrows: a bit above max value
   const arrowY = new Array(meteo.length).fill((maxValue || 1) * 1.1);

   // "Now" vertical line and label (only if inside time range)
   const now = new Date();
   const shapes = [];
   const annotations = [];

   if (xDates.length > 0) {
      const minDate = xDates[0];
      const maxDate = xDates[xDates.length - 1];

      if (now >= minDate && now <= maxDate) {
         shapes.push({
            type: 'line',
            xref: 'x',
            yref: 'y',
            x0: now,
            x1: now,
            y0: 0,
            y1: (maxValue || 1) * 1.2,
            line: {
               color: 'black',
               width: 2,
               dash: 'dot'
            }
         });

         annotations.push({
            x: now,
            y: (maxValue || 1) * 1.2,
            xref: 'x',
            yref: 'y',
            text: 'now',
            showarrow: false,
            font: {
               size: 10,
               color: 'black'
            },
            yanchor: 'bottom'
         });
      }
   }

   // Plotly traces with custom hover templates and wind direction info
   const gustTrace = {
      name: 'Gust kt',
      x: xDates,
      y: gustArray,
      customdata: twdDirArray,
      mode: 'lines+markers',
      line: { width: 2, color: 'red' },
      marker: { size: 4 },
      hovertemplate:
         'Gust: %{y:.2f} kt<br>' +
         '<extra></extra>'
   };

   const windTrace = {
      name: 'Wind speed kt',
      x: xDates,
      y: twsArray,
      customdata: twdDirArray,
      mode: 'lines+markers',
      line: { width: 2, color: 'blue' },
      marker: { size: 4 },
      hovertemplate:
         'Wind: %{customdata}° %{y:.2f} kt' +
         '<extra></extra>'
   };

   const waveTrace = {
      name: 'Wave height m',
      x: xDates,
      y: waveArray,
      customdata: twdDirArray,
      mode: 'lines+markers',
      line: { width: 2, color: 'green' },
      marker: { size: 4 },
      yaxis: 'y',
      hovertemplate:
         'Wave: %{y:.2f} m' +
         '<extra></extra>'
   };

   // Direction arrows at top of graph
   const arrowTrace = {
      name: 'Wind direction',
      x: xDates,
      y: arrowY,
      mode: 'text',
      text: arrowText,
      textfont: { size: 14, color: 'gray' },
      hoverinfo: 'skip',
      showlegend: false
   };

   const layout = {
      margin: { l: 60, r: 20, t: 40, b: 80 },
      xaxis: {
         title: 'Local date and time',
         tickformat: '%Y-%m-%d %H:%M',
         nticks: 20,      // limit number of ticks to avoid crowding
         tickangle: -45
      },
      yaxis: {
         title: 'Wind kt - Waves m',
         rangemode: 'tozero'
      },
      legend: {
         orientation: 'h',
         x: 0,
         y: 1.1
      },
      hovermode: 'x unified',
      shapes: shapes,
      annotations: annotations
   };

   const footerHtml = 
      `${latLonToStr (lat, lon, DMSType)},&nbsp;&nbsp;` +
      `${epochToStrDate (epochStart)} locale,&nbsp;&nbsp;` +
      `Sea: ${boolToYesNo(data.isSea)},&nbsp;&nbsp;` +
      `Sea tolerant: ${boolToYesNo(data.isSeaTolerant)},&nbsp;&nbsp;` +
      `Wind: ${boolToYesNo(data.inWind)},&nbsp;&nbsp;` +
      `Current: ${boolToYesNo(data.inCurrent)},&nbsp;&nbsp;` +
      `grib: ${data.grib}`;

   // SweetAlert2 dialog
   Swal.fire({
      title: 'Meteogram',
      width: '90%',
      html: '<div id="meteogram-plot" style="width: 100%; height: 60vh;"></div>',
      footer: footerHtml,
      didOpen: () => {
         const div = document.getElementById('meteogram-plot');
         Plotly.newPlot(div, [gustTrace, windTrace, waveTrace, arrowTrace], layout, {
            responsive: true
         });
      }
   });
}

/**
 * SweetAlert2 prompt for credentials
 */
async function promptForCreds() {
   // simple HTML escape for values put into attributes
   const html = `
      <form> 
      <input id="swal-user" class="swal2-input" placeholder="User ID" autocomplete="${userId}" value="${esc(userId)}">
      <input id="swal-pass" class="swal2-input" type="password" placeholder="Password" autocomplete="${password}" value="${esc(password)}">
      <!-- <label style="display:flex;align-items:center;gap:.5rem;margin:.25rem 1.25rem 0;">
         <input id="swal-show" type="checkbox"> Show password
      </label> -->
      </form>
   `;

   const res = await Swal.fire({
      title: "Sign in",
      html,
      focusConfirm: false,
      showDenyButton: true,
      showCancelButton: true,
      confirmButtonText: "Sign in",
      denyButtonText: "Continue as guest",
      cancelButtonText: "Cancel",
      didOpen: () => {
         const $popup = Swal.getPopup();
         const $user = $popup.querySelector("#swal-user");
         const $pass = $popup.querySelector("#swal-pass");
         $user && $user.focus();
      },
      preConfirm: () => {
         const $popup = Swal.getPopup();
         const user = $popup.querySelector("#swal-user").value.trim();
         const pass = $popup.querySelector("#swal-pass").value;
         if (!user || !pass) {
            Swal.showValidationMessage("User ID and password are required to sign in.");
            return false;
         }
         return { user, pass };
      }
   });
   if (res.isConfirmed && res.value) {
      userId = res.value.user;
      password = res.value.pass;
      return "signed-in";
   }
   if (res.isDenied) {
      userId = "anonymous";
      password = "anonymous";
      return "guest";
   }
   return "cancelled";
   // throw new Error("cancelled");
}

/**
 * Get Grib name from server, fit map in grib bounds and display init Info UNUSED
 */
function showInitMessage (language = 'fr') {
   const messages = {
      fr: {
         title: "Information",
         text: `Connexion au serveur réussie. <br>
Clic droit pour déplacer le bateau et fixer une destination avec ou sans waypoints.<br>
Menu <b>Route/Launch</b> pour lancer un routage.`,
         button: "🇬🇧 English"
      },
      en: {
         title: "Information",
         text: `The server is online.<br>
Right click to move the boat and to choose a destination with or without waypoints.<br>
Menu <b>Route/Launch</b> to launch route calculation.`,
         button: "🇫🇷 Français"
      }
   };
   Swal.fire({
      title: messages[language].title,
      html: messages[language].text,
      icon: "info",
      showCancelButton: true,
      confirmButtonText: messages[language].button,
      cancelButtonText: "OK",
      //buttonsStyling: false,
      customClass: {
         confirmButton: 'swal-init-confirm',
         cancelButton: 'swal-init-cancel'
      }
   }).then((result) => {
      if (result.isConfirmed) {
         oldShowInitMessage(language === 'fr' ? 'en' : 'fr');
      }
   });
}

/**
 * Get Grib name from server, fit map in grib bounds and display init Info
 */
function getServerInit () {
   const formData = `type=${REQ.PAR_JSON}`;
   console.log ("Request sent:", formData);
   fetch (apiUrl, {
      method: "POST",
      headers: {
         "Content-Type": "application/x-www-form-urlencoded"
      },
      body: formData
   })
   .then(response => response.json())
   .then(data => {
      console.log ("JSON received:", data);
      // Dialog box display
      gribLimits.bottomLat = data.bottomLat;
      gribLimits.leftLon = data.leftLon;
      gribLimits.topLat = data.topLat;
      gribLimits.rightLon = data.rightLon;
      gribLimits.name = data.grib;
      updateStatusBar ();
      // showInitMessage ();
      const bounds = [[gribLimits.bottomLat, gribLimits.leftLon],[gribLimits.topLat, gribLimits.rightLon]];
      map.fitBounds(bounds);
      // alert (`bottomLat: ${gribLimits.bottomLat}, leftLon: ${gribLimits.leftLon}, topLat: ${gribLimits.topLat}, rigthtLon: ${gribLimits.rightLon}`);
   })
   .catch (error => {
      console.error("Error Init:", error);
      Swal.fire("Erreur", "Impossible to access server", "error");
   });
}

function helpInfoHtml(data, full) {
  const head = `
    <style>
      .swal-links { color:#444; text-decoration:none; font-weight:bold; }
      .swal-links:hover { text-decoration:underline; color:#222; }
    </style>
    <strong>Rcube:</strong><br>
    <strong>Version:</strong> 1.0.0<br><br>
    <strong>© 2025 rene.rigault@wanadoo.fr</strong><br><br>
  `;

  const bodyFull = `
    <strong>Références :</strong><br>
    <a href="https://www.windy.com/" class="swal-links" target="_blank">Windy</a><br>
    <a href="https://leafletjs.com/" class="swal-links" target="_blank">Leaflet</a><br>
    <strong>from server:</strong><br>
    ${data["Prog-version"]}<br>
    Conf File: ${data["Conf File"]}<br>
    GRIB Reader: ${data["Grib Reader"]}<br>
    GRIB Wind Memory: ${data["Memory for Grib Wind"]}<br>
    GRIB Current Memory: ${data["Memory for Grib Current"]}<br>
    API server port: ${data["API server port"]}<br>
    Memory usage in KB: ${data["Memory usage in KB"]}<br>
    Authorization-Level: ${data["Authorization-Level"]}<br>
    Client IP address: ${data["Client IP Address"]}<br>
    User Agent: ${data["User Agent"]}<br>
    Compilation-date: ${data["Compilation-date"]}<br>
    Client side Windy model: ${window.map && store ? store.get('product') : "NA"}
  `;

  return full ? head + bodyFull : head; // court = seulement l'en-tête
}

/**
 * Display help Info
 * Retrieve some info from server
 */
async function helpInfo (full = false) {
   const formData = `type=${REQ.TEST}`;
   const headers = { "Content-Type": "application/x-www-form-urlencoded" };
   console.log ("Request sent:", formData);
   fetch (apiUrl, {
      method: "POST",
      headers,
      body: formData,
      cache: "no-store"
   })
   .then(response => response.json())
   .then(data => {
      console.log (JSON.stringify(data));
      // Dialog box display
      Swal.fire({
         title: "Help Info",
         html:  helpInfoHtml(data, full),
         icon: "info",
         showCancelButton: true,
         confirmButtonText: full ? "Less" : "More",
         customClass: { popup: "swal-wide" },
      }).then((result) => {
         if (result.isConfirmed) helpInfo(!full);
      });
   })
   .catch (error => {
      console.error("Error requesting help:", error);
      Swal.fire("Erreur", "Impossible to access server", "error");
   });
}


function getTIndex (index, boatName) {
   const theTime = getDateFromIndex (index, boatName);
   const diffHours = (theTime.getTime() - gribLimits.epochStart * 1000) / 3600000;
   const {iTInf, iTSup} = findTimeAround (diffHours);
   const iTime =  (gribLimits.timeStamps [iTSup] - diffHours) < (diffHours - gribLimits.timeStamps [iTInf]) ? iTSup : iTInf; 
   return iTime;
}

/**
 * Render wind barbs on the custom Leaflet canvas layer.
 *
 * This function draws a wind field using GRIB data on a canvas placed inside a
 * dedicated Leaflet pane. The canvas is automatically resized and aligned with
 * the current map viewport using geographic bounds (map.getBounds()).
 *
 * For each GRIB grid point, the function:
 *   1. Converts the (lat, lon) coordinate to a Leaflet layer point.
 *   2. Transforms it into the local canvas coordinate system.
 *   3. Applies optional thinning to avoid overdraw at low zoom levels.
 *   4. Draws a wind barb symbol representing U/V wind components.
 *
 * The canvas always stays visually below popups and markers, but above the base map.
 * Wind symbols update automatically when the map moves or zooms.
 *
 * Requirements:
 *   - `windCanvas` must be appended to a Leaflet pane via `map.createPane()`.
 *   - `dataGrib` must provide `getUVGW(timeIndex, latIndex, lonIndex)`.
 *   - `gribLimits` describes grid geometry (lat/lon steps, bounds, sizes).
 *   - `drawWindBarb(ctx, x, y, u, v)` must be defined elsewhere.
 *
 * Behavior:
 *   - Skips drawing points outside the visible map bounds.
 *   - Performs screen-space culling (one barb per 25px grid by default).
 *   - Reduces the number of barbs depending on map zoom (via `getWindStride()`).
 *
 * Called automatically from:
 *   `map.on('moveend zoomend resize', drawWind)`
 *
 * @function drawWind
 * @returns {void}
 */
function drawWind() {
  if (!route || !dataGrib || !dataGrib.getUVGW) return;
  const boatName = Object.keys(route)[0]; // Extract first key from response
  if (!route[boatName].track || route[boatName].track.length == 0
      || !dataGrib || !gribLimits || !gribLimits.timeStamps) return;

  const ctx = windCanvas.getContext('2d');
  const {
    nTimeStamp, nLat, nLon,
    bottomLat, topLat, leftLon, rightLon,
    latStep, lonStep, nShortName
  } = gribLimits;
  console.log ("nTimeStamp: ", nTimeStamp, "nShortName: ", nShortName);

  const len0 = nTimeStamp * nLat * nLon * nShortName; // u v g w
  const len1 = dataGrib.values.length;
  if (len0 != len1) {
      console.warn ("In drawWind: unconsistent dataGrib length");
      Swal.fire ("Unexpected grib size", `Value Length: ${len1}, Expected: ${len0},`, "error");
      return;
  }

  // 🔴 use visible geographic bounds
  const mapBounds = map.getBounds(); // LatLngBounds
  const topLeft     = map.latLngToLayerPoint(mapBounds.getNorthWest());
  const bottomRight = map.latLngToLayerPoint(mapBounds.getSouthEast());
  const size        = bottomRight.subtract(topLeft);

  // Positionner et dimensionner le canvas
  L.DomUtil.setPosition(windCanvas, topLeft);
  windCanvas.width  = size.x;
  windCanvas.height = size.y;

  ctx.clearRect(0, 0, windCanvas.width, windCanvas.height);

  const zoom = map.getZoom();
  const stride = getWindStride(zoom);

  const cellSize = 25; // px
  const usedCells = new Set();
  const iTimeStamp = getTIndex(index, boatName);

  for (let iLat = 0; iLat < nLat; iLat += stride) {
    const lat = bottomLat + iLat * latStep;

    for (let iLon = 0; iLon < nLon; iLon += stride) {
      const lon = leftLon + iLon * lonStep;

      // Optionnel : petit culling géographique pour ne pas traiter tout le globe
      if (lat < mapBounds.getSouth() - 1 || lat > mapBounds.getNorth() + 1 ||
          lon < mapBounds.getWest() - 1  || lon > mapBounds.getEast() + 1) {
        continue;
      }

      const { u, v } = dataGrib.getUVGW(iTimeStamp, iLat, iLon);
      const latLng   = L.latLng(lat, lon);

      // Coordonnées en layer
      const pt = map.latLngToLayerPoint(latLng);

      // Ramener dans le repère du canvas (origine = topLeft)
      const x = pt.x - topLeft.x;
      const y = pt.y - topLeft.y;

      // Culling écran
      if (x < 0 || y < 0 || x > windCanvas.width || y > windCanvas.height) continue;

      const cx = Math.floor(x / cellSize);
      const cy = Math.floor(y / cellSize);
      const key = cx + "," + cy;
      if (usedCells.has(key)) continue;
      usedCells.add(key);

      drawWindBarb(ctx, x, y, u, v);
    }
  }
}

