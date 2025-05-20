/* jshint esversion: 6 */

/**
 * Calculates the number of sail changes and tack/gybe transitions (amure changes)
 * from a given sailing track.
 *
 * A sail change is detected when the `sail` value changes between two consecutive steps.
 * An amure change is detected when the sign of the True Wind Angle (`twa`) changes,
 * indicating a tack or gybe (crossing the wind).
 *
 * @param {Array<Array<number>>} track - The sailing track, where each entry is an array
 *   containing [lat, lon, dist, sog, twd, tws, hdg, twa, g, w, stamina, sail, motor].
 *
 * @returns {{nSailChange: number, nAmureChange: number}} An object with the number of sail changes
 *   and amure changes detected in the track.
 */
function statChanges (track) {
   const NIL = -10000;
   let nSailChange = 0;
   let nAmureChange = 0;
   let oldSail = NIL;
   let oldTwa = NIL; 
   for (let i = 0; i < track.length; i++) {
      let [indexWp, lat, lon, time, dist, sog, twd, tws, hdg, twa, g, w, stamina, sail, motor] = track[i];
      if (oldSail !== NIL && oldSail !== sail) nSailChange += 1;
      if (oldTwa !== NIL && oldTwa * twa < 0) nAmureChange += 1;
      oldSail = sail;
      oldTwa = twa;
   }
   return { nSailChange, nAmureChange };
}

/**
 * Displays a graphical representation of the route data using Plotly.
 * 
 * @param {Object} routeData - The dataset containing route information.
 */
function showRouteReport (routeData) {
   let boatName;
   if (routeData)
      boatName = Object.keys(routeData)[0]; // Extract first key from response
   if (!routeData || !boatName || !routeData[boatName]) {
      Swal.fire({
         icon: 'error',
         title: 'Route Not Found',
         text: `The route for "${boatName}" does not exist.`,
      });
      return;
   }

   let boat = routeData [boatName];
   let durationFormatted = formatDuration(boat.duration);
   let trackPoints = boat.track.length;
   let isocTimeStep = boat.isocTimeStep || 3600; // Default 1 hour if not defined

   // Extracting data
   let times = [];
   let sogData = [];
   let twsData = [];
   let gustData = [];
   let waveData = [];
   let windArrows = [];
   let maxY = 0;
   let sailLineSegments = []; // horizontal sail line
   let ySailLine = -1; //display line a little bit under 
   let legendTracker = new Set();
   let wpChangeLines = []; // for vertivcal Wypoint Change
   let previousWp = null;
   // calculate maxY
   for (let i = 0; i < boat.track.length; i++) {
      let [indexWp, lat, lon, time, dist, sog, twd, tws, hdg, twa, g, w, stamina, sail, motor] = boat.track[i];
      maxY = Math.max (maxY, sog, tws, g * MS_TO_KN);
   }

   for (let i = 0; i < boat.track.length; i++) {
      let [indexWp, lat, lon, time, dist, sog, twd, tws, hdg, twa, g, w, stamina, sail, motor] = boat.track[i];
      // let currentTime = new Date(routeParam.startTime.getTime() + i * isocTimeStep * 1000);
      let currentTime = new Date(routeParam.startTime.getTime() + time * 1000);
      if (i === 0) console.log ("showRouteReport Local Start Time: " + currentTime);
      times.push (currentTime);

      if (indexWp !== previousWp && previousWp !== null) {
         wpChangeLines.push({
            x: [currentTime, currentTime],
            y: [0, maxY],
            mode: 'lines',
            line: {
               color: 'black', 
               width: 3,
               dash: 'dash'
            },
            hoverinfo: 'text',
            text: ['Next WP', 'Next WP'], // une pour chaque point
            showlegend: false
         });
      }
      previousWp = indexWp;
      //sogData.push(Math.round(sog * 100) / 100);
      sogData.push (sog.toFixed (2));
      twsData.push (tws.toFixed (2));
      gustData.push ((g * MS_TO_KN).toFixed (2)); // Convert m/s → knots
      waveData.push (w.toFixed (2));
      windArrows.push ({ time: currentTime, twd });

      if (!sailLegend[sail]) continue;
      // for sail line
      let color = sailLegend[sail].bg;
      let dash = (twa >= 0) ? 'solid' : 'dot';
      let bord = (twa >= 0) ? 'Tribord' : 'Babord';
      let hoverText = `${sail}<br>${bord}`;
      let t0 = times [i - 1];
      let t1 = times [i];

      sailLineSegments.push({
         x: [t0, t1],
         y: [ySailLine, ySailLine],
         mode: 'lines',
         line: { color, width: 4, dash },
         name: sail,
         hoverinfo: 'text',
         text: [hoverText, hoverText],
         showlegend: false
      });
   }

   // Define the graph traces
   let traces = [
      ...wpChangeLines, // Vertical lines for WayPoint change
      ...sailLineSegments, // Horizontal line for sail and amure
      { x: times, y: sogData, mode: 'lines', name: 'Speed (Kn)', line: { color: 'black' },
        hovertemplate: 'Speed: %{y} Kn<br>%{x}<extra></extra>' },
      { x: times, y: gustData, mode: 'lines', name: 'Gusts (Kn)', line: { color: 'red' },
         hovertemplate: 'Gusts: %{y} Kn<br>%{x}<extra></extra>' },
      { x: times, y: twsData, mode: 'lines', name: 'Wind (Kn)', line: { color: 'blue' },
        hovertemplate: 'Wind: %{y} Kn<br>%{x}<extra></extra>'},
      { x: times, y: waveData, mode: 'lines', name: 'Waves (m)', line: { color: 'green' },
         hovertemplate: 'Waves: %{y} m<br>%{x}<extra></extra>' }
   ];
   let modulo = Math.round (boat.track.length / 20);
   if (modulo < 1) modulo = 1;
   // Add wind direction arrows only for valid twd values
   let annotations = windArrows
   .filter(({ twd }, i) => !isNaN(twd) && (i % modulo === 0)) // Ensure twd is a number and limit the number of display
   .map(({ time, twd }, i) => ({
      x: time, 
      // y: twsData[i] + 1, // Position above wind speed curve
      y: maxY,
      text: '→',
      showarrow: false,
      font: { size: 16, color: 'gray', family: 'Arial' },
      ax: 0, ay: 0,
      textangle: 90 + twd
   }));

   let reachable = (boat.destinationReached) ? 'Destination Reached' : 'Destination unreached';  

   let layout = {
      title: `${reachable} for ${boatName}`,
      xaxis: { 
         title: 'Time',
         tickformat: '%H:%M\n%d-%b', // Display hours + dates
         type: 'date'
      },
      yaxis: { title: 'Values' },
      annotations: annotations
   };

   // Create the graph container and ensure full width
   let graphContainer = document.createElement('div');
   graphContainer.style = "width: 100vw; height: 500px; margin: 0 auto;"; // Ensure full width
   Plotly.newPlot (graphContainer, traces, layout);

   // Create a container for metadata
   let metaDataContainer = document.createElement('div');
   metaDataContainer.style = "display: flex; justify-content: space-around; width: 100%; margin-top: 20px;";

   let metaDataLeft = document.createElement('div');
   const lastDate = new Date(routeParam.startTime.getTime() + boat.duration * 1000);
   const formattedStartDate = dateToStr (routeParam.startTime);
   const formattedLastDate = dateToStr (lastDate);
   const isocTimeStepFormatted = formatDurationShort (isocTimeStep);
   metaDataLeft.innerHTML = `
      <div style="text-align: left; max-width: 500px;">
         <strong>🛥️ Boat:</strong> ${boatName} <br>
         <strong>🚀 Start Time:</strong> ${formattedStartDate}<br>
         <strong>🎯 ETA:</strong> ${formattedLastDate}<br>
         <strong>⏳ Duration:</strong> ${durationFormatted} <br>
         <strong>📏 Distance:</strong> ${boat.totDist} NM <br>
         <strong>⚙️  Motor Distance:</strong> ${boat.motorDist} NM <br>
         <strong>📈  Average Speed:</strong> ${(3600 * boat.totDist/boat.duration).toFixed (2)} Kn <br>
         <strong>⌚ Isoc Time Step:</strong> ${isocTimeStepFormatted} <br>
         <strong>📍 Steps:</strong> ${trackPoints} <br>
         <strong>🖥️ Calculation Time:</strong> ${boat.calculationTime} s <br>
      </div>
   `;
   let { nSailChange, nAmureChange } = statChanges(boat.track);
   let metaDataRight = document.createElement('div');
   metaDataRight.innerHTML = `
      <div style="text-align: left; max-width: 300px;">
         <strong>N Sail Changes:</strong> ${nSailChange} <br>
         <strong>N Amure Changes:</strong> ${nAmureChange} <br>
         <strong>🌊 Polar:</strong> ${boat.polar} <br>
         <strong>Wave Polar:</strong> ${boat.wavePolar} <br>
         <strong>📄 Grib:</strong> ${boat.grib} <br>
         <strong>Current Grib:</strong> ${boat.currentGrib} <br>
      </div>
   `;
   // Append elements to the main container
   metaDataContainer.appendChild(metaDataLeft);
   metaDataContainer.appendChild(metaDataRight);

   // Create the final container
   let container = document.createElement('div');
   container.style = "display: flex; flex-direction: column; align-items: center; width: 100vw; max-width: 95vw; margin: 0 auto;";
   container.appendChild (graphContainer);
   container.appendChild (metaDataContainer);

   // Show Swal with full-width settings
   Swal.fire({
      title: '📊 Route Description',
      html: '<div id="swal-container"></div>',
      icon: 'info',
      confirmButtonText: 'OK',
      confirmButtonColor: 'orange',
      background: '#fefefe',
      showCloseButton: true,
      width: '95vw',       // Ensures full width
      heightAuto: false,   // Prevents automatic height limitation
      didOpen: () => {
         document.getElementById('swal-container').appendChild(container);
         // Force Plotly to adjust after Swal opens
         Plotly.relayout(graphContainer, { 'width': window.innerWidth * 0.9 });
      }
   });
}

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
function dumpRoute (routeData) {
   let boatName;
   let options = {
      year: 'numeric', month: '2-digit', day: '2-digit',
      hour: '2-digit', minute: '2-digit',
   hour12: false
   };

   if (routeData)
      boatName = Object.keys(routeData)[0]; // Extract first key from response
   if (!routeData || !boatName || !routeData[boatName]) {
      Swal.fire({
         icon: 'error',
         title: 'Route Not Found',
         text: `The route for "${boatName}" does not exist.`,
      });
      return;
   }

   let boat = routeData[boatName];
   let isocTimeStep = boat.isocTimeStep || 3600; // Default time step in seconds
   const lastDate = new Date (routeParam.startTime.getTime() + boat.duration * 1000);
   let currentTime;
   let len = boat.track.length;  
   // Prepare table data
   let tableData = boat.track.map((point, index) => {
      if (!Array.isArray(point) || point.length < 10) {
         console.warn(`Invalid track data at index ${index}:`, point);
         return null; // Skip invalid data
      }

      let [indexWp, lat, lon, time, dist, sog, twd, tws, hdg, twa, g, w, stamina, sail, motor, id, father] = point;
      return {
         Step: index,
         WP: indexWp === -1 ? `<span style="color:green">Dest.</span>`
             : indexWp % 2 === 0 ? `<span style="color:red">${indexWp}</span>` 
             : `${indexWp}`,
         Coord: toDMSString (lat, lon),
         'Date Time': dateToStr ( new Date (routeParam.startTime.getTime() + time * 1000)),
         //Sail: sail ?? '-',
         Sail: (() => {
            let sailName, entry, bg;
            if (motor) {
               sailName = 'Motor';
               entry = {bg: 'lightgray', luminance: 200};
               fg = 'black';
            }
            else {
               sailName = sail ?? 'NA';
               entry = sailLegend [sailName] || { bg: 'lightgray', luminance: 200 };
               fg = getTextColorFromLuminance(entry.luminance);
            }
            return `<span style="background-color:${entry.bg}; color:${fg}; padding:2px 6px; border-radius:4px;">${sailName}</span>`;
         })(),

         DIST: dist !== undefined ? dist.toFixed(2) : '-',
         SOG: sog !== undefined ? sog.toFixed(2) : '-',
         TWD: twd !== undefined ? `${Math.round(twd)}°` : '-',
         TWS: tws !== undefined ? tws.toFixed(2) : '-',
         HDG: hdg !== undefined ? `${Math.round(hdg)}°` : '-',
         TWA: twa !== undefined
               ? `<span style="color:${twa >= 0 ? 'green' : 'red'}">${Math.round(twa)}°</span>`
               : '-',
         Gust: g !== undefined ? (g * MS_TO_KN).toFixed(2) : '-', // Convert gusts to knots
         Waves: w !== undefined ? w.toFixed(2) : '-',
         Stamina: stamina !== undefined ? stamina : '-',
         Id: id !== undefined ? id : '-',
         Father: father !== undefined ? father : '-'
      };
   }).filter(row => row !== null); // Remove null values
   
   let trackPoints = boat.track.length;
   let { nSailChange, nAmureChange } = statChanges(boat.track);
   const durationFormatted = formatDuration (boat.duration);
   const formattedStartDate = dateToStr (routeParam.startTime);
   const formattedLastDate = dateToStr (lastDate);
   const isocTimeStepFormatted = formatDurationShort (isocTimeStep);
   // Metadata information
   let metadataHTML = `
      <div style="display: flex; justify-content: space-between; padding: 0 30px";>
      <div style="text-align: left; margin-bottom: 15px; margin-left: 50px;">
         <strong>Boat:</strong> ${boatName} <br>
         <strong>Start Time:</strong> ${formattedStartDate}<br>
         <strong>ETA:</strong> ${formattedLastDate}<br>
         <strong>Duration:</strong> ${durationFormatted} <br>
         <strong>Distance:</strong> ${boat.totDist} NM <br>
         <strong>Motor Distance:</strong> ${boat.motorDist} NM <br>
         <strong>Average Speed:</strong> ${(3600 * boat.totDist/boat.duration).toFixed (2)} Kn <br>
         <strong>Isoc Time Step:</strong> ${isocTimeStepFormatted} <br>
         <strong>Steps:</strong> ${trackPoints} <br>
         <strong>lastStepDuration:</strong> ${lastStepDurationFormatted (boat.lastStepDuration)} <br>
         <strong>Calculation Time:</strong> ${boat.calculationTime} s <br>
      </div>
      <div style="text-align: left; max-width: 300px;">
         <strong>N Sail Changes:</strong> ${nSailChange} <br>
         <strong>N Amure Changes:</strong> ${nAmureChange} <br>
         <strong>Polar:</strong> ${boat.polar} <br>
         <strong>Wave Polar:</strong> ${boat.wavePolar} <br>
         <strong>Grib:</strong> ${boat.grib} <br>
         <strong>Current Grib:</strong> ${boat.currentGrib} <br>
      </div>
      </div>
   `;

   // Create a table dynamically
   let tableContainer = document.createElement('div');
   let tableHTML = metadataHTML + `<table border="1" style="width: calc(100% - 40px); margin: 0 20px; text-align: center; border-collapse: collapse;">
      <thead>
         <tr>
            <th>Step</th><th>WP</th><th>Coord.</th><th>Date Time</th>
            <th>Sail</th><th>Dist. (NM)</th><th>SOG (Kn)</th><th>HDG</th><th>TWD</th><th>TWA</th>
            <th>TWS (Kn)</th><th>Gust (Kn)</th><th>Waves (m)</th><th>Stamina % </th>
            <th>ID</th><th>Father</th>
         </tr>
      </thead>
      <tbody>`;

   tableData.forEach(row => {
      tableHTML += `<tr>
         <td>${row.Step}</td><td>${row.WP}</td><td>${row.Coord}</td>
         <td>${row['Date Time']}</td><td>${row.Sail}</td><td>${row.DIST}</td>
         <td>${row.SOG}</td><td>${row.HDG}</td><td>${row.TWD}</td><td>${row.TWA}</td><td>${row.TWS}</td>
         <td>${row.Gust}</td><td>${row.Waves}</td><td>${Math.round (row.Stamina)}</td>
         <td>${row.Id}</td> <td>${row.Father}</td>
      </tr>`;
   });

   tableHTML += '</tbody></table>';
   tableContainer.innerHTML = tableHTML;
   let reachable = (boat.destinationReached) ? 'Destination Reached' : 'Destination unreached';  
   if (boat.routingRet === -1) reachable += " ERROR in route"; 
   // Display table in a Swal popup
   Swal.fire({
      title: `${reachable} for ${boatName}`,
      html: tableContainer,
      icon: 'info',
      confirmButtonText: 'OK',
      confirmButtonColor: 'orange',
      background: '#fefefe',
      showCloseButton: true,
      width: '95vw', // Ensure full width
      heightAuto: false
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
      Swal.fire({
         icon: "warning",
         title: "Isodesc not available",
         text: "No isochrone descriptor (_isodesc) found in the provided route data."
      });
      return;
   }

   const headers = [
      "nIsoc",
      "WayPoint",
      "size",
      "first",
      "closest",
      "bestVmc",
      "biggestOrthoVmc",
      "focal"
   ];

   const rows = route._isodesc.map(row => {
      const [
         nIsoc,
         wayPoint,
         size,
         first,
         closest,
         bestVmc,
         biggestOrthoVmc,
         focalLat,
         focalLon
      ] = row;

      const latLonStr = latLonToStr (focalLat, focalLon);

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
      confirmButtonText: "Close"
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
      Swal.fire({
         icon: "warning",
         title: "Isochrone not available",
         text: "No isochrone (_isoc) found in the provided route data."
      });
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
         const coordStr = latLonToStr(lat, lon);
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
      confirmButtonText: "Close"
   });
}

