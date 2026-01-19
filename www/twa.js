/* jshint esversion: 6 */
let twaRoutesGroup = L.featureGroup(); //: container global

/**
 * Opens a SweetAlert2 modal allowing the user to configure
 * TWA routing parameters (boat, start time, weather model, options, etc).
 *
 * The dialog is pre-filled using the current values stored in `routeParam`.
 * When confirmed, the values are validated, stored back into `routeParam`,
 * and a new TWA routing request is sent to the server.
 *
 * This function also handles:
 *  - Reset to default values
 *  - Cancel without applying changes
 *  - UI → internal state synchronization
 *
 * Side effects:
 *  - Updates `routeParam`
 *  - Calls `updateStatusBar()`
 *  - Triggers `twaRoute()`
 *
 * @returns {void}
 */
function launchTwaRouting () {
   const boatNames = competitors.map(c => c.name);
   const getNowISOString = (date = new Date()) => {
      return new Date(date.getTime() - date.getTimezoneOffset() * 60000).toISOString().slice(0, 16);
   };
   const current = {
      startTimeStr: routeParam.startTime
         ? getNowISOString(new Date(routeParam.startTime))  // (après correction de la fonction)
         : getNowISOString(), // <-- au premier lancement seulement
      isoStep: routeParam.isoStep ?? 1800,
      iBoat: routeParam.iBoat ?? 1,
      withWaves: routeParam.withWaves ?? false,
      withCurrent: routeParam.withCurrent ?? false,
      nSteps: routeParam.nSteps ?? 10,
      twa: routeParam.twa ?? 90,
      dayEfficiency: routeParam.dayEfficiency ?? 1.0,
      nightEfficiency: routeParam.nightEfficiency ?? 1.0    
   };
   const boatOptions = 
      boatNames.map((name, i) => {
         const val = i + 1;
         const selected = (val === current.iBoat) ? "selected" : "";
         return `<option value="${val}" ${selected}>${name}</option>`;
      }).join("");

   const htmlContent = `
   <div class="swal-grid">
      <label for="swal-startTime">Date & Time:</label>
      <input type="datetime-local" id="swal-startTime" value="${current.startTimeStr}">

      <label for="swal-isoStep">Time Step:</label>
      <select id="swal-isoStep">
         <option value="900"${current.isoStep === 900 ? " selected" : ""}>15 minutes</option>
         <option value="1800"${current.isoStep === 1800 ? " selected" : ""}>30 minutes</option>
         <option value="3600"${current.isoStep === 3600 ? " selected" : ""}>1 hour</option>
         <option value="10800"${current.isoStep === 10800 ? " selected" : ""}>3 hours</option>
      </select>

      <label for="swal-boatSelect">Boat:</label>
      <select id="swal-boatSelect">${boatOptions}</select>

      <label for="swal-model">Model:</label>
      <select id="swal-model">
         <option value="GFS">GFS</option>
         <option value="ECMWF">ECMWF</option>
         <option value="ARPEGE">ARPEGE</option>
         <option value="UCMC">METEOCONSULT</option>
         <option value="SYN">SYN</option>
      </select>

      <label for="swal-nSteps">N Steps:</label>
      <input type="number" id="swal-nSteps" min="0" max="100" value="${current.nSteps}">

      <label for="swal-twa">TWA:</label>
      <input type="number" id="swal-twa" min="-180" max="180" value="${current.twa}">

      <label for="swal-dayEff">Day Efficiency:</label>
      <input type="number" id="swal-dayEff" min="0.50" max="1.50" step="0.01" value="${current.dayEfficiency || ''}">

      <label for="swal-nightEff">Night Efficiency:</label>
      <input type="number" id="swal-nightEff" min="0.50" max="1.50" step="0.01" value="${current.nightEfficiency || ''}">

      <label for="swal-withWaves">With Waves:</label>
      <div><input type="checkbox" id="swal-withWaves" ${current.withWaves ? "checked" : ""}></div>

      <label for="swal-withCurrent">With Current:</label>
      <div><input type="checkbox" id="swal-withCurrent" ${current.withCurrent ? "checked" : ""}></div>
   </div>

   <div class="swal-footer">
      <button type="button" id="resetBtn" class="swal2-deny swal2-styled">Reset</button>
      <button type="button" class="swal2-confirm swal2-styled" data-swal2-confirm>Launch Route</button>
      <button type="button" class="swal2-cancel swal2-styled" data-swal2-cancel>Cancel</button>
   </div>`;
   Swal.fire({
      title: 'TWA Route Parameters',
      html: htmlContent,
      customClass: {
         popup: 'swal-wide'
      },
      showConfirmButton: false,
      didOpen: () => {
         document.getElementById("resetBtn").addEventListener("click", () => {
            document.getElementById("swal-startTime").value = getNowISOString();
            document.getElementById("swal-isoStep").value = "1800";
            document.getElementById("swal-model").value = "GFS";
            document.getElementById("swal-boatSelect").value = "1";
            document.getElementById("swal-withWaves").checked = false;
            document.getElementById("swal-withCurrent").checked = false;
            document.getElementById("swal-nSteps").value = "10";
            document.getElementById("swal-twa").value = "90";
            document.getElementById("swal-dayEff").value = "1"; 
            document.getElementById("swal-nightEff").value = "1";

         });

         Swal.getPopup().querySelector('[data-swal2-cancel]').addEventListener("click", () => {
            Swal.close();
         });

         Swal.getPopup().querySelector('[data-swal2-confirm]').addEventListener("click", () => {
            setTimeout(() => {
               const startTimeStr = document.getElementById("swal-startTime").value;
               const isoStep = parseInt(document.getElementById("swal-isoStep").value, 10);
               const iBoat = parseInt(document.getElementById("swal-boatSelect").value, 10);
               const model = document.getElementById("swal-model").value;
               const withWaves = document.getElementById("swal-withWaves").checked;
               const withCurrent = document.getElementById("swal-withCurrent").checked;
               const nSteps = parseInt(document.getElementById("swal-nSteps").value, 10) || 0;           
               const twa = parseInt(document.getElementById("swal-twa").value, 10) || 90;               
               const dayEfficiency = parseFloat(document.getElementById("swal-dayEff").value) || 1;
               const nightEfficiency  = parseFloat(document.getElementById("swal-nightEff").value) || 1;

               routeParam.startTimeStr = startTimeStr;
               routeParam.startTime = new Date(startTimeStr);
               routeParam.isoStep = isoStep;
               routeParam.iBoat = iBoat;
               routeParam.withWaves = withWaves;
               routeParam.withCurrent = withCurrent;
               routeParam.nSteps = nSteps;
               routeParam.model = model;
               routeParam.twa = twa;
               routeParam.dayEfficiency = dayEfficiency;
               routeParam.nightEfficiency = nightEfficiency;

               updateStatusBar();
               console.log ("Updated TWA Route Parameters:", routeParam);
               Swal.close();
               twaRoute();
            }, 0); // <<< important here
         });
      }
   });
}

/**
 * Removes all currently displayed TWA routes from the Leaflet map.
 * @returns {void}
 */
function clearTwaRoutes() {
  if (twaRoutesGroup) {
    twaRoutesGroup.clearLayers();
  }
}

/**
 * Builds the HTTP POST body used to request a TWA route from the server.
 *
 * The returned string is formatted as
 * `application/x-www-form-urlencoded`.
 *
 * It includes:
 *  - Boat name and position
 *  - Start time (epoch)
 *  - Time step
 *  - Polar and wave polar
 *  - Weather model
 *  - Routing options (waves, current, efficiencies, TWA, nSteps)
 *  - Optional GRIB file names
 *
 * @param {Object} c
 *        Selected competitor (boat).
 * @param {string} c.name
 * @param {number} c.lat
 * @param {number} c.lon
 *
 * @param {Object} routeParam
 *        Global routing parameters object.
 *
 * @returns {string}
 *          URL-encoded request body ready for fetch().
 */
function buildBodyTwa (c, routeParam) {
   const reqParams = {
      type: REQ.TWA,
      boat: `${c.name},${c.lat},${c.lon};`,
      timeStep: (routeParam.isoStep ?? 1800),
      epochStart: Math.floor(routeParam.startTime.getTime() / 1000),
      polar: `pol/${polarName}`,
      wavePolar: `wavepol/${polWaveName}`,
      withWaves: (routeParam.withWaves ?? "false"),
      withCurrent: (routeParam.withCurrent ?? "false"),
      dayEfficiency: isNaN(routeParam.dayEfficiency ?? NaN) ? 1.0 : routeParam.dayEfficiency,
      nightEfficiency: isNaN(routeParam.nightEfficiency ?? NaN) ? 1.0 : routeParam.nightEfficiency,
      model: routeParam.model,
      twa: routeParam.twa ?? 90,
      nSteps: routeParam.nSteps ?? 10
   };

   let requestBody = Object.entries(reqParams)
      .map(([key, value]) => `${key}=${value}`)
      .join("&");
   
   if (gribLimits.name && typeof gribLimits.name === "string" && gribLimits.name.trim().length > 1) {
      // requestBody += `&grib=grib/${gribLimits.name}`; // name of grib can be deduced by the sever thanks to model specification
      if (gribLimits.currentName.trim().length > 1)
         requestBody +=`&currentGrib=currentgrib/${gribLimits.currentName}`;
   }
   
   return requestBody;
}

/**
 * Sends a TWA routing request to the REST API and renders the result.
 *
 * This function:
 *  1. Builds the POST body using `buildBodyTwa()`
 *  2. Sends the request to `apiUrl`
 *  3. Parses the returned JSON
 *  4. Displays errors if the server reports any
 *  5. Draws the TWA route on the Leaflet map
 *  6. Opens the detailed route dump (table + graphs)
 *
 * On success, it creates:
 *  - A dashed orange polyline for the route
 *  - A marker on the final waypoint
 *  - A new `twaLayerGroup` for easy cleanup
 *
 * @async
 * @returns {Promise<void>}
 */
async function twaRoute() {
  const headers = { "Content-Type": "application/x-www-form-urlencoded" };
  const requestBody = buildBodyTwa (competitors[routeParam.iBoat - 1], routeParam);
  console.log (requestBody);
  console.log (routeParam.startTimeStr);  
  console.log (routeParam.startTime);

  const response = await fetch(apiUrl, {
    method: "POST",
    headers,
    body: requestBody,
    cache: "no-store"
  });
  if (!response.ok) {
     const txt = await response.text();
     throw new Error(`HTTP ${response.status}: ${txt}`);
  }
  const data = await response.json();
  console.log (JSON.stringify(data));

  if (data._Error) {
    console.error("Server error:", data._Error);
    Swal.fire("TWA Routing error from Server", data._Error, "error");
    return;
  }
  // data.array = [[lat, lon], [lat, lon], ...]
  const latlngs = data.array.map(([lat, lon]) => [lat, lon]);

  dumpTwaRoute (data);

  // Polyline pointillée orange
  const line = L.polyline(latlngs, {
    color: "orange",
    weight: 3,
    opacity: 0.9,
    dashArray: "6 8",     // pointillé
    lineCap: "round"
  });

  // mark last point
  const durationFormatted = formatDuration(data.duration);
  const last = latlngs[latlngs.length - 1];
  let marker = L.marker(last, {}).addTo(map);
  marker.bindPopup (`TWA: ${data.twa}°<br>Dist: ${data.totDist}NM<br>Duration: ${durationFormatted}`);

  twaRoutesGroup.addLayer(line);
  twaRoutesGroup.addLayer(marker);

  // Zoom on route
  map.fitBounds(line.getBounds(), { padding: [20, 20] });
}

/**
 * Displays a detailed dump of a TWA route using SweetAlert2.
 *
 * The modal contains:
 *  - Summary information (distance, duration, speed, sail changes)
 *  - A Plotly time-series graph (speed, wind, gusts, waves, sails)
 *  - A detailed table of each routing step
 *
 * This function assumes the server returned data in the TWA JSON format:
 *  - data.array = [[lat, lon, t, dist, hdg, twd, tws, gust, waves, ..., sail], ...]
 *
 * @param {Object} data
 *        Parsed JSON response from the TWA routing API.
 *
 * @returns {void}
 */
function dumpTwaRoute(data) {
  const startTime = data.epochStart;
  const lastTime = startTime + data.timeStep * data.nSteps;

  const durationFormatted = formatDuration(data.duration);
  const formattedStartDate = dateToStr(new Date(startTime * 1000));
  const formattedLastDate  = dateToStr(new Date(lastTime * 1000));

  // --- Build table HTML (string) (OK) ---
  let tableData = data.array.map((point) => {
    let [lat, lon, t, d, hdg, twd, tws, g, w, , , sail] = point;
    return {
      Coord: latLonToStr(lat, lon, DMSType),
      DateTime: dateToStr(new Date((startTime + t) * 1000)),
      Sail: (() => {
        let sailName = sail ?? "NA";
        let entry = sailLegend[sailName.toUpperCase()] || { bg: "lightgray", luminance: 200 };
        let fg = getTextColorFromLuminance(entry.luminance);
        return `<span style="background-color:${entry.bg}; color:${fg}; padding:2px 6px; border-radius:4px;">${sailName}</span>`;
      })(),
      DIST: d !== undefined ? d.toFixed(2) : "-",
      TWD: twd !== undefined ? `${Math.round(twd)}°` : "-",
      TWS: tws !== undefined ? tws.toFixed(2) : "-",
      HDG: hdg !== undefined ? `${Math.round(hdg)}°` : "-",
      Gust: g !== undefined ? (g * MS_TO_KN).toFixed(2) : "-",
      Waves: w !== undefined ? w.toFixed(2) : "-",
    };
  });

  const metadataHTML = `
    <div style="font-size: 8px;line-height: 1.35;font-family: monospace;">
      <div style="margin-bottom: 15px; font-size: 11px;">
        <strong>TotalDist:</strong> ${data.totDist} NM 
        <strong>Duration:</strong> ${durationFormatted}  
        <strong>Average Speed:</strong> ${(3600 * data.totDist / data.duration).toFixed(2)} Kn
        <strong>Sail Changes:</strong> ${data.nSailChange} &nbsp;
      </div>
    </div>
  `;

  let rowsHTML = "";
  tableData.forEach(row => {
    rowsHTML += `<tr>
      <td>${row.Coord}</td><td>${row.DateTime}</td><td>${row.DIST}</td><td>${row.HDG}</td><td>${row.TWD}</td>
      <td>${row.TWS}</td><td>${row.Gust}</td><td>${row.Waves}</td><td>${row.Sail}</td>
    </tr>`;
  });

  const tableHTML = `
    ${metadataHTML}
    <div id="plotWrap" style="width:100%; height:500px; box-sizing:border-box;"></div>
    <table border="1" style="width: calc(100% - 40px); margin: 0 20px; text-align: center; border-collapse: collapse;">
      <thead><tr>
        <th>Coord.</th><th>Date Time</th><th>Dist (NM)</th><th>HDG</th><th>TWD</th><th>TWS (Kn)</th><th>Gust (Kn)</th><th>Waves (m)</th><th>Sail</th>
      </tr></thead>
      <tbody>${rowsHTML}</tbody>
    </table>
  `;

  const timeStepFormatted = formatDurationShort(data.timeStep);
  const footer = `Time Step: ${timeStepFormatted}, nSteps: ${data.nSteps},\
                   Polar: ${data.polar}, Wave Polar: ${data.wavePolar}\
                   Grib: ${data.grib}, Current Grib: ${data.currentGrib}`;

  const title = `Dump TWA route<br><i><small>TWA: ${data.twa}, Start Time: ${formattedStartDate}, &nbsp; ETA: ${formattedLastDate}</small></i></br>`;

  Swal.fire({
    title,
    html: tableHTML,            // ✅ string OK
    background: "#fefefe",
    showCloseButton: true,
    width: "95vw",
    footer,
    heightAuto: false,
    didOpen: () => {
      // ✅ container is now in DOM → Plotly can size correctly
      const plotDiv = Swal.getHtmlContainer().querySelector("#plotWrap");
      buildGraph(data, plotDiv);
    }
  });
}

/**
 * Builds and renders the Plotly graph for a TWA route.
 *
 * The graph shows:
 *  - Boat speed (SOG)
 *  - True wind speed (TWS)
 *  - Gusts
 *  - Wave height
 *  - Sail usage as colored horizontal segments
 *  - Wind direction arrows above the timeline
 *
 * The X axis is time (Date objects).
 *
 * @param {Object} data
 *        TWA route data returned by the server.
 *
 * @param {HTMLElement} graphContainer
 *        DOM element where the Plotly graph will be rendered.
 *
 * @returns {void}
 */
function buildGraph (data, graphContainer) {
  const startTime = data.epochStart;

  const times = [];
  const sogData = [];
  const twsData = [];
  const gustData = [];
  const waveData = [];
  const windArrows = [];
  const sailLineSegments = [];

  const ySailLine = -1;

  for (let i = 0; i < data.array.length; i++) {
    const [,, time, dist,, twd, tws, g, w,,, sail] = data.array[i];
    const sog = (time > 0) ? dist / (3600 * time) : 0;
    const currentTime = new Date((startTime + time) * 1000);
    times.push(currentTime);

    // keep NUMBERS (not strings)
    sogData.push(Math.round(sog * 100) / 100);
    twsData.push(Math.round(tws * 100) / 100);
    gustData.push(Math.round(g * MS_TO_KN * 100) / 100);
    waveData.push(Math.round(w * 100) / 100);
    windArrows.push({ time: currentTime, twd });

    // sail segments only if i >= 1 (need t0)
    const sailInfo = sailLegend?.[String(sail).toUpperCase()];
    if (sailInfo && i >= 1) {
      const color = sailInfo.bg;
      const dash = "solid";
      const hoverText = `${sail}`;

      sailLineSegments.push({
        x: [times[i - 1], times[i]],
        y: [ySailLine, ySailLine],
        mode: "lines",
        line: { color, width: 4, dash },
        hoverinfo: "text",
        text: [hoverText, hoverText],
        showlegend: false
      });
    }
  }

  const traces = [
    ...sailLineSegments,

    { x: times, y: sogData, mode: "lines", name: "Speed (Kn)", line: { color: "black" },
      hovertemplate: "Speed: %{y} Kn<br>%{x}<extra></extra>" },

    { x: times, y: gustData, mode: "lines", name: "Gust (Kn)", line: { color: "red" },
      hovertemplate: "Gust: %{y} Kn<br>%{x}<extra></extra>" },

    { x: times, y: twsData, mode: "lines", name: "Wind (Kn)", line: { color: "blue" },
      hovertemplate: "Wind: %{y} Kn<br>%{x}<extra></extra>" },

    { x: times, y: waveData, mode: "lines", name: "Waves (m)", line: { color: "green" },
      hovertemplate: "Waves: %{y} m<br>%{x}<extra></extra>" }
  ];

  // arrows density
  let modulo = Math.round(data.array.length / 20);
  if (modulo < 1) modulo = 1;

  const annotations = windArrows
    .filter(({ twd }, i) => Number.isFinite(twd) && (i % modulo === 0))
    .map(({ time, twd }) => ({
      x: time,
      xref: "x",
      y: 1.02,          // juste au-dessus du plot
      yref: "paper",
      text: "→",
      showarrow: false,
      font: { size: 16, color: "gray", family: "Arial" },
      textangle: 90 + twd,
      xanchor: "center",
      yanchor: "bottom",
      cliponaxis: false // important : ne pas couper au bord du plot
    }));
  
  const layout = {
    autosize: true,
    margin: { l: 40, r: 16, t: 55, b: 60 },

    xaxis: { title: "", tickformat: "%H:%M\n%d-%b", type: "date", automargin: false },
    yaxis: { automargin: false, tickfont: { size: 12 } },

    legend: {
      orientation: "h",
      x: 0, xanchor: "left",
      y: 1.18, yanchor: "bottom",
      font: { size: 12 },
      itemwidth: 95,   // ✅ 4 traces on one line
      itemsizing: "constant"
    },
    //paper_bgcolor: "#f5efe6",
    plot_bgcolor:  "#f5efe6",

    annotations
  };

  const plotlyConf = {
    responsive: true,
    displaylogo: false,
    displayModeBar: false
  };

  Plotly.newPlot(graphContainer, traces, layout, plotlyConf);
}
