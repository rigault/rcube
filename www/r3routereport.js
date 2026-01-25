/* jshint esversion: 6 */

/**
 * Analyze a sailing track and render a pie/donut chart of time spent
 * on each point of sail.
 *
 * @param {Array} track - Array of points, each point shaped like:
 *   [indexWp, lat, lon, time, dist, sog, twd, tws, cog, twa, g, w, stamina, sail, motor]
 * @param {string} containerId - DOM element id where the Plotly chart will be drawn
 * @returns {Object} stats - Raw stats including time buckets and totalTime
 */
function statRoute(track, containerId) {
   // Accumulators of time spent (in seconds) per point of sail
   const buckets = {
      pres: 0,
      bonPlein: 0,
      travers: 0,
      largue: 0,
      ventArriere: 0
   };

   // Walk segment by segment [i -> i+1] to estimate duration per heading range
   for (let i = 0; i < track.length - 1; i++) {
      let [ , , , timeI, , , , , , twaI ] = track[i];
      let [ , , , timeNext ] = track[i + 1];

      let dt = timeNext - timeI;
      if (!Number.isFinite(dt) || dt < 0) {
         // skip broken segment
         continue;
      }
      let twaAbs = Math.abs(twaI);
      if (twaAbs <= 55) buckets.pres += dt; 
      else if (twaAbs <= 80) buckets.bonPlein += dt;
      else if (twaAbs <= 100) buckets.travers += dt;
      else if (twaAbs <= 160) buckets.largue += dt;
      else if (twaAbs <= 180) buckets.ventArriere += dt;
   }

   const labels = [
      "Près (≤55°)",
      "Bon plein (55°–80°)",
      "Travers (80°–100°)",
      "Largue (100°–160°)",
      "Vent arrière (160°–180°)"
   ];

   const values = [
      buckets.pres,
      buckets.bonPlein,
      buckets.travers,
      buckets.largue,
      buckets.ventArriere
   ];

   const totalTime = values.reduce((a, b) => a + b, 0);

   // Draw donut only if Plotly is available and we got a container
   if (containerId && typeof Plotly !== "undefined") {
      const data = [{
         type: "pie",
         hole: 0.4, // donut style
         labels: labels,
         values: values,
         sort: false,           // keep logical sail order
         direction: "clockwise",
         textinfo: "none",      // no text directly on slices -> cleaner
         hovertemplate:
            "%{label}" +
            "<br>%{percent:.1%} Time" +
            "<br>%{value:.0f} s<extra></extra>",
         showlegend: true,
         legend: { orientation: "h" }
      }];

      const layout = {
         // no title here; Swal title is enough
         height: 320,
         margin: { t: 10, b: 10, l: 10, r: 10 },
         showlegend: true,
         legend: {
            orientation: "h",
            x: 0.5,
            xanchor: "center",
            y: -0.05
         }
      };
      const plotlyConf = {
        responsive: true,
        displaylogo: false,
        displayModeBar: false,
        staticPlot: isMobile()
      };
 
      Plotly.newPlot(containerId, data, layout, plotlyConf);
   }

   return { buckets, totalTime };
}

/**
 * Show a SweetAlert2 modal with the route stats
 * (donut chart + summary table).
 *
 * @param {Object} routeData 
 */
function displayStatRoute(routeData, type) {
   // Basic validation
   if (!routeData) {
      Swal.fire("Error", `The route does not exist.`, "error");
      return;
   }

   const track = routeData.track;
   if (!Array.isArray(track) || track.length < 2) {
      Swal.fire("Error", `No usable track data.`, "error");
      return;
   }

   Swal.fire({
      title: `Points of sails`,
      html: `
         <div style="display:flex; flex-direction:column; gap:1rem; align-items:center; width:100%;">
            <div id="routeStatsPlot" style="width:320px; height:320px;"></div>
            <div id="routeStatsText"style="font-size:0.9rem; width:100%; max-width:360px;"></div>
         </div>
      `,
      width: 520,
      showConfirmButton: true,
      showCancelButton: true,
      confirmButtonText: "Back",

      didOpen: () => {
         statRoute(track, "routeStatsPlot");
      }
   }).then ((res) => {if (res.isConfirmed) showRouteReport (routeData, type)});  
}

/*
 * Build Html meta datas for routeData
 *
 * @param {Object} routeData 
 */
function buildMeta (routeData) {
  const total = routeData.totDist;
  if (total < 0) return;
  const pMoteur  = (routeData.motorDist  / total * 100).toFixed(0);
  const pTribord = (routeData.starboardDist / total * 100).toFixed(0);
  const pBabord  = (routeData.portDist  / total * 100).toFixed(0);

  const content =
    `<div style="width:100%; margin:0; padding:6px 0; text-align:center;">
        <div style="font-size:11px; display:flex; justify-content:center; gap:10px; margin:0;">
           <div style="margin:0;"><span style="display:inline-block;width:10px;height:10px;
              border-radius:2px;background:#999;margin-right:4px;"></span>Motor ${pMoteur}% ${routeData.motorDist} NM</div>
           <div style="margin:0;"><span style="display:inline-block;width:10px;height:10px;
              border-radius:2px;background:green;margin-right:4px;"></span>Starboard ${pTribord}% ${routeData.starboardDist} NM</div>
           <div style="margin:0;"><span style="display:inline-block;width:10px;height:10px;
              border-radius:2px;background:red;margin-right:4px;"></span>Port ${pBabord}% ${routeData.portDist} NM</div>
        </div>
        <div style="font-size:11px; margin:0;">
           <strong>TotalDist:</strong> ${total} NM, 
           <strong>Average Speed:</strong> ${(3600 * total / routeData.duration).toFixed(2)} Kn<br>
           <strong>Sail Changes:</strong> ${routeData.nSailChange} &nbsp;
           <strong>Amure Changes:</strong> ${routeData.nAmureChange}
        </div>
     </div>
   `;
   return content;
}

/**
 * Displays a graphical representation of the route data using Plotly.
 * 
 * @param {Object} routeData - The dataset containing route information.
 */
function buildRouteGraph (routeData, graphContainer) {
  const durationFormatted = formatDuration(routeData.duration);
  const trackPoints = routeData.track.length;
  const isocTimeStep = routeData.isocTimeStep || 3600;
  const startTime = routeData.epochStart;

  const times = [];
  const sogData = [];
  const twsData = [];
  const gustData = [];
  const waveData = [];
  const windArrows = [];
  const sailLineSegments = [];
  const wpChangeLines = [];

  let maxY = 0;
  let previousWp = null;
  const ySailLine = -1;

  // maxY
  for (let i = 0; i < routeData.track.length; i++) {
    const row = routeData.track[i];
    const sog = row[5];
    const tws = row[7];
    const g   = row[10];
    maxY = Math.max(maxY, sog, tws, g * MS_TO_KN);
  }

  for (let i = 0; i < routeData.track.length; i++) {
    const [indexWp,,, time,, sog, twd, tws,, twa, g, w,, sail, ] = routeData.track[i];

    const currentTime = new Date((startTime + time) * 1000);
    times.push(currentTime);

    // waypoint change marker (skip first)
    if (indexWp !== previousWp && previousWp !== null) {
      wpChangeLines.push({
        x: [currentTime, currentTime],
        y: [0, maxY],
        mode: "lines",
        line: { color: "black", width: 3, dash: "dash" },
        hoverinfo: "text",
        text: ["Next WP", "Next WP"],
        showlegend: false
      });
    }
    previousWp = indexWp;

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
      const dash = (twa >= 0) ? "solid" : "dot";
      const bord = (twa >= 0) ? "Tribord" : "Babord";
      const hoverText = `${sail}<br>${bord}`;

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
    ...wpChangeLines,
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
  let modulo = Math.round(routeData.track.length / 20);
  if (modulo < 1) modulo = 1;

  const annotations = windArrows
    .filter(({ twd }, i) => Number.isFinite(twd) && (i % modulo === 0))
    .map(({ time, twd }) => ({
      x: time,
      y: maxY,
      text: "→",
      showarrow: false,
      font: { size: 16, color: "gray", family: "Arial" },
      textangle: 90 + twd
    }));

  const mobile = isMobile();

  const layout = {
    autosize: true,
    margin: mobile ? { l: 28, r: 12, t: 55, b: 55 } : { l: 30, r: 10, t: 20, b: 20 },

    xaxis: { title: "", tickformat: "%H:%M\n%d-%b", type: "date", automargin: false },
    yaxis: { automargin: false, tickfont: { size: mobile ? 10 : 12 } },

    legend: {
      orientation: "h",
      x: 0, xanchor: "left",
      y: 1.18, yanchor: "bottom",
      font: { size: mobile ? 10 : 12 },
      itemwidth: mobile ? 70 : 95,   // ✅ 4 traces on one line
      itemsizing: "constant"
    },
    plot_bgcolor:  "#f5efe6",

    annotations
  };

  const plotlyConf = {
    responsive: true,
    displaylogo: false,
    displayModeBar: false,
    staticPlot: mobile
  };

  Plotly.newPlot(graphContainer, traces, layout, plotlyConf);
};

/**
 * Formats an array of durations (in seconds) into a single string separated by commas.
 * @param {number[]} durationArray - An array of durations in seconds.
 * @returns {string} A comma-separated string of formatted durations.
 */
function lastStepDurationFormatted (durationArray) {
   if (!Array.isArray(durationArray) || durationArray.length === 0) return '';
   return durationArray.map(formatDurationShort).join(', ');
}

/**
 * Displays a detailed table of the boat's route data in a modal.
 * The table includes step index, latitude, longitude, timestamp, sail number, motor status,
 * speed over ground (SOG), true wind angle (TWA), true wind speed (TWS), gust speed, and wave height.
 * 
 * @param {Object} routeData - The complete dataset containing route information.
 */
function showRouteReport(routeData, locDMSType = DMS_DISPLAY.DMS) {
   if (!routeData) {
      Swal.fire({
         icon: 'error',
         title: 'Route Not Found',
         text: `The route does not exist.`,
      });
      return;
   }
   const mobile = isMobile ();
   const startTime = routeData.epochStart;
   let isocTimeStep = routeData.isocTimeStep || 3600; // Default time step in seconds
   const lastDate = new Date ((startTime + routeData.duration) * 1000);
   let oldStamina = 100;
   // Prepare table data
   let tableData = routeData.track.map((point, index) => {
      if (!Array.isArray(point) || point.length < 10) {
         console.warn(`Invalid track data at index ${index}:`, point);
         return null; // Skip invalid data
      }
      let [indexWp, lat, lon, time, dist, sog, twd, tws, cog, twa, g, w, stamina, sail, motor, id, father] = point;
      let changeLow =  (stamina < oldStamina); 
      oldStamina = stamina;
      return {
         Step: index,
         WP: indexWp === -1 ? `<span style="color:green">Dest.</span>`
             : indexWp % 2 === 0 ? `<span style="color:red">${indexWp}</span>` 
             : `${indexWp}`,
         Coord: latLonToStr (lat, lon, locDMSType),
         DateTime: dateToStr ( new Date ((startTime + time) * 1000)),
         Sail: (() => {
            let sailName, entry;
            if (motor) {
               sailName = 'Motor';
               entry = {bg: 'lightgray', luminance: 200};
               fg = 'black';
            }
            else {
               sailName = sail ?? 'NA';
               entry = sailLegend [sailName.toUpperCase()] || { bg: 'lightgray', luminance: 200 };
               fg = getTextColorFromLuminance(entry.luminance);
            }
            return `<span style="background-color:${entry.bg}; color:${fg}; padding:2px 6px; border-radius:4px;">${sailName}</span>`;
         })(),

         DIST: dist !== undefined ? dist.toFixed(2) : '-',
         SOG: sog !== undefined ? sog.toFixed(2) : '-',
         TWD: twd !== undefined ? `${Math.round(twd)}°` : '-',
         TWS: tws !== undefined ? tws.toFixed(2) : '-',
         COG: cog !== undefined ? `${Math.round(cog)}°` : '-',
         TWA: twa !== undefined
               ? `<span style="color:${twa >= 0 ? 'green' : 'red'}">${Math.round(twa)}°</span>`
               : '-',
         Gust: g !== undefined ? (g * MS_TO_KN).toFixed(2) : '-', // Convert gusts to knots
         Waves: w !== undefined ? w.toFixed(2) : '-',
         Stamina: stamina !== undefined
               ? `<span style="color:${stamina >= 100 ? 'green' : changeLow ? 'red' : 'black'}">${Math.round(stamina)}</span>`
               : '-',
         Id: id !== undefined ? id : '-',
         Father: father !== undefined ? father : '-'
      };
   }).filter(row => row !== null); // Remove null values
   
   let trackPoints = routeData.track.length;
   const durationFormatted = formatDuration (routeData.duration);
   const startDate = new Date (startTime * 1000);
   const formattedStartDate = dateToStr (startDate);
   const formattedLastDate = dateToStr (lastDate);
   const isocTimeStepFormatted = formatDurationShort (isocTimeStep);

   // Create a table dynamically
   let tableContainer = document.createElement('div');
   let tableHTML = `
      <table border="1" style="width: calc(100% - 40px); margin: 0 20px; text-align: center; border-collapse: collapse;">
      
      <thead>
         <tr>
            <th>Step</th><th>WP</th><th>Coord.</th><th>Date Time</th>
            <th>Sail</th><th>Dist. (NM)</th><th>SOG (Kn)</th><th>COG</th><th>TWD</th><th>TWA</th>
            <th>TWS (Kn)</th><th>Gust (Kn)</th><th>Waves (m)</th><th>Stamina % </th>
            <th>ID</th><th>Father</th>
         </tr>
      </thead>
      <tbody>`;

   tableData.forEach(row => {
      tableHTML += `<tr>
         <td>${row.Step}</td><td>${row.WP}</td><td>${row.Coord}</td>
         <td>${row.DateTime}</td><td>${row.Sail}</td><td>${row.DIST}</td>
         <td>${row.SOG}</td><td>${row.COG}</td><td>${row.TWD}</td><td>${row.TWA}</td><td>${row.TWS}</td>
         <td>${row.Gust}</td><td>${row.Waves}</td><td>${row.Stamina}</td>
         <td>${row.Id}</td> <td>${row.Father}</td>
      </tr>`;
   });

   tableHTML += '</tbody></table></div>';
   tableContainer.innerHTML = tableHTML;
   let reachable = (routeData.destinationReached) 
      ? `🎯 Destination Reached after ${durationFormatted}` 
      : `😩 Unreached: ${routeData.distToDest} NM to destination.`;
   reachable += ` <br><i><small>Start Time: ${formattedStartDate}, &nbsp; ETA: ${formattedLastDate}</small></i>`;

   if (routeData.routingRet === -1) reachable += " ERROR in route"; 

   const mainContainer = document.createElement("div");
   mainContainer.style.display = "flex";
   mainContainer.style.flexDirection = "column";
   mainContainer.style.gap = "12px";
   
   const plotWrap = document.createElement("div");
   plotWrap.id = "plotWrap";
   plotWrap.style.width = "100%";
   plotWrap.style.height = "400px";
   
   const metaDataContainer = document.createElement("div");
   metaDataContainer.className = "route-metadata";
   metaDataContainer.innerHTML = buildMeta(routeData);

   mainContainer.appendChild(plotWrap);
   mainContainer.appendChild(metaDataContainer);
   if (!mobile) mainContainer.appendChild(tableContainer);

   const footer = `Calculation Time: ${routeData.calculationTime} s, \
                   Isoc Time Step: ${isocTimeStepFormatted}, Steps: ${trackPoints},\
                   lastStepDuration: ${lastStepDurationFormatted (routeData.lastStepDuration)},\
                   Polar: ${routeData.polar}, Wave Polar:</strong> ${routeData.wavePolar}\
                   Grib: ${routeData.grib}, Current Grib: ${routeData.currentGrib}`;
   // Display table in a Swal popup
   Swal.fire({
      title: reachable,
      html: mainContainer,
      background: '#fefefe',
      confirmButtonText: "Stat",
      showCloseButton: true,
      showCancelButton: true,
      width: '95vw', // Ensure full width
      customClass: { popup: "swal-fullscreen-plot", title: mobile ? "swal-title-mobile" : "" },      
      footer: mobile ? null : footer,
      heightAuto: false,
      didOpen: () => {
        // ✅ container is now in DOM → Plotly can size correctly
         const plotDiv = Swal.getHtmlContainer().querySelector("#plotWrap");
         buildRouteGraph(routeData, plotDiv);
       }
   }).then(res => {
      if (res.isConfirmed) displayStatRoute(routeData, locDMSType);
  });
}

/**
 * Displays a table of isochrone descriptors from a routing result using SweetAlert2.
 *
 * The table includes the following fields for each isochrone: nIsoc, WayPoint, size, first, 
 * closest, bestVmc, biggestOrthoVmc, focalLat (DMS), and focalLon (DMS).
 *
 * @param {Object} route - The JSON object containing isochrone descriptors under `_isodesc`.
 * @returns {void}
 */
function dumpIsoDesc(route) {
   if (!route || !route._isodesc) {
      Swal.fire("Isodesc not available", "No isochrone descriptor (_isodesc) found in the provided route data.", "warning");
      return;
   }

   const headers = ["nIsoc", "WayPoint", "size", "first", "closest", "bestVmc", "biggestOrthoVmc", "focal"];

   const rows = route._isodesc.map(row => {
      const [nIsoc, wayPoint, size, first, closest, bestVmc, biggestOrthoVmc, focalLat, focalLon ] = row;
      const latLonStr = latLonToStr (focalLat, focalLon, DMSType).replaceAll ("'", "&apos;");

      return [
         nIsoc,
         wayPoint,
         size,
         first,
         closest,
         bestVmc.toFixed(2),
         biggestOrthoVmc.toFixed(2),
         latLonStr
      ];
   });

   let html = `<div style="overflow:auto; max-height:70vh;"><table style="border-collapse: collapse; width: 100%; font-family: monospace;">`;

   // Table header
   html += "<thead><tr>";
   headers.forEach(h => {
      html += `<th style="border: 1px solid #ccc; padding: 4px; text-align: center;">${h}</th>`;
   });
   html += "</tr></thead><tbody>";

   // Table rows
   rows.forEach(row => {
      html += "<tr>";
      row.forEach(cell => {
         html += `<td style="border: 1px solid #ccc; padding: 4px; text-align: center;">${cell}</td>`;
      });
      html += "</tr>";
   });

   html += "</tbody></table></div>";

   Swal.fire({
      title: "Isochrone Descriptors",
      html: html,
      width: "80%",
      showCloseButton: true
   });
}

/**
 * Displays a table of isochrones from a routing result using SweetAlert2.
 *
 * The table includes the following fields for each point in isochrone: lat, lon, id, father, Index
 * prefixed by the isochrone number
 *
 * @param {Object} route - The JSON object containing isochrones under `_isoc`.
 * @returns {void}
 */
function dumpIsoc (route) {
   if (!route || !route._isoc) {
      Swal.fire("Isochrone not available", "No isochrone (_isoc) found in the provided route data.", "warning");
      return;
   }

   const isocs = route._isoc;
   const headers = ["nIso", "Coord", "ID", "Father", "Index"];
   const rows = [];

   // Build the rows
   for (let i = 0; i < isocs.length; i++) {
      const isoc = isocs[i];
      for (let j = 0; j < isoc.length; j++) {
         const point = isoc[j];
         const lat = point[0];
         const lon = point[1];
         const id = point[2];
         const father = point[3];
         const index = point[4];
         const coordStr = latLonToStr(lat, lon, DMSType).replaceAll ("'", "&apos;");
         rows.push([i, coordStr, id, father, index]);
      }
   }

   // Construct HTML table
   let html = `<div style="overflow:auto; max-height:70vh;"><table style="border-collapse: collapse; width: 100%; font-family: monospace;">`;

   // Table header
   html += "<thead><tr>";
   headers.forEach(h => {
      html += `<th style="border: 1px solid #ccc; padding: 4px; text-align: center;">${h}</th>`;
   });
   html += "</tr></thead><tbody>";

   // Table rows
   rows.forEach(row => {
      html += "<tr>";
      row.forEach(cell => {
         html += `<td style="border: 1px solid #ccc; padding: 4px; text-align: center;">${cell}</td>`;
      });
      html += "</tr>";
   });

   html += "</tbody></table></div>";

   Swal.fire({
      title: "Isochrone Dump",
      html: html,
      width: "80%",
      showCloseButton: true
   });
}

/** fetch GPX server route 
 */
function showGpxRoute () {
   const formData = `type=${REQ.GPX_ROUTE} `;
   console.log("Request sent:", formData);
   const headers = { "Content-Type": "application/x-www-form-urlencoded" };
   fetch (apiUrl, {
      method: "POST",
      headers,
      body: formData,
      cache: "no-store"
   })
   .then(response => {
      console.log ("Raw response:", response);
      if (!response.ok) {
         throw new Error(`Error ${response.status}: ${response.statusText}`);
      }
      return response.text();
   })
   .then(data => {
      const content = `<pre style="text-align: left; font-family: 'Courier New', Courier, monospace; font-size: 14px;">${esc(data)}</pre>`;

      Swal.fire({
         title: "GPX Route",
         html: content,
         width: "60%",
         showCloseButton: true
      });
   })
   .catch(error => {
      console.error("Catched error:", error);
      Swal.fire("Error", error.message, "error");
   });
}

