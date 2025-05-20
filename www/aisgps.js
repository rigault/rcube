/** Global AIS datas */
let aisData = null;

/** Global layer group to manage AIS markers */
let aisLayer = null;

/** The current GPS marker */
let gpsMarker = null;

/** Array of GPS coordinates for the track */
let gpsTrack = [];

/** The polyline showing the GPS track */
let gpsPolyline = null;

/**
 * Displays the current GPS position on the map.
 * Updates the marker, popup, and the track polyline.
 * 
 * @param {Object} gpsData - The GPS data in JSON format.
 * @param {number} gpsData.lat - Latitude in decimal.
 * @param {number} gpsData.lon - Longitude in decimal.
 * @param {string} gpsData.time - UTC time string.
 * @param {number} [gpsData["alt M"]] - Altitude in meters (optional).
 * @param {number} gpsData.sog - Speed over ground.
 * @param {number} gpsData.cog - Course over ground.
 * @param {number} gpsData.numSat - Number of satellites.
 */
function showBoatPosition (gpsData) {
   const lat = gpsData.lat;
   const lon = gpsData.lon;
   const dms = toDMSString (lat, lon);

   const altStr = gpsData["alt m"] !== undefined
      ? `Altitude: ${gpsData["alt m"]} m<br>`
      : "";

   const popupContent = `
      <b>GPS</b><br>
      Coordinates: ${dms}<br>
      ${altStr}
      SOG: ${gpsData.sog} kn<br>
      COG: ${gpsData.cog}°<br>
      Satellites: ${gpsData.numSat}<br>
      Time: ${gpsData.time}
   `;

   // Emoji icon for the marker
   const emojiIcon = L.divIcon({
      html: '📍',
      iconSize: [24, 24],
      className: 'gps-icon'
   });

   // Remove previous marker if exists
   if (gpsMarker) {
      map.removeLayer(gpsMarker);
   }
   if (gpsData.time === "NA" || gpsData.numSat < 2)
      return;

   // Add new marker with popup
   gpsMarker = L.marker([lat, lon], { icon: emojiIcon })
      .addTo(map)
      .bindPopup(popupContent);
      //.openPopup();

   // Add point to GPS track
   gpsTrack.push([lat, lon]);

   // Update or create the polyline
   /*if (gpsPolyline) {
      gpsPolyline.setLatLngs(gpsTrack);
   } else {
      gpsPolyline = L.polyline(gpsTrack, { color: 'red' }).addTo(map);
   }*/
   // Optional: center the map on the position
   // map.setView([lat, lon]);
}

/**
 * Fetches the current GPS position from the local server.
 * Uses a timeout to prevent hanging if the server is unresponsive.
 */
function fetchGpsPosition() {
   const controller = new AbortController();
   const timeout = setTimeout(() => {
      controller.abort(); // Cancel request after 5s
   }, 5000);

   fetch (gpsUrl, {
      method: 'GET',
      signal: controller.signal
   })
   .then(response => {
      clearTimeout(timeout);
      console.log('Server response status:', response.status);
      if (!response.ok) {
         throw new Error(`HTTP error ${response.status}`);
      }
      return response.text(); // read as text first to see raw output
   })
   .then(text => {
      console.log('Raw response from server:', text);

      let data;
      try {
         data = JSON.parse(text);
      } catch (e) {
         console.error('Failed to parse JSON:', e.message);
         if (gpsMarker) map.removeLayer(gpsMarker);
         return;
      }

      console.log('Parsed JSON:', data);
      showBoatPosition(data);
   })
   .catch(err => {
      clearTimeout(timeout);
      console.error('Fetch error:', err.message);
      if (gpsMarker) map.removeLayer(gpsMarker);
   });
}

/**
 * Display AIS data in a Swal2 modal as a spreadsheet-like table.
 * @param {Object[]} aisData - Array of AIS objects to display.
 */
function aisDump (aisData) {
   if (aisData === null) {
      Swal.fire ('AIS Warning', 'No AIS Data', 'warning');
      return;
   }
   // Generate table headers
   const headers = ['Name', 'MessageID', 'Country', 'Min Dist', 'MMSI', 'Coordinate', 'SOG', 'COG', 'Last Update'];
  
   // Build table HTML
   let html = '<div style="overflow-x:auto;"><table style="width:100%; border-collapse:collapse; text-align:left;">';
   html += '<thead><tr>';
   for (const header of headers) {
      html += `<th style="border:1px solid #ccc; padding:4px;">${header}</th>`;
   }
   html += '</tr></thead><tbody>';

   // Populate rows
   for (const item of aisData) {
      const cog = item.cog < 0 ? item.cog + 360 : item.cog;
      html += '<tr>';
      html += `<td style="border:1px solid #ccc; padding:4px;">${item.name}</td>`;
      html += `<td style="border:1px solid #ccc; padding:4px;">${item.messageId}</td>`;
      html += `<td style="border:1px solid #ccc; padding:4px;">${item.country}</td>`;
      html += `<td style="border:1px solid #ccc; padding:4px;">${item.mindist}</td>`;
      html += `<td style="border:1px solid #ccc; padding:4px;">${item.mmsi}</td>`;
      html += `<td style="border:1px solid #ccc; padding:4px;">${toDMSString (item.lat, item.lon)}</td>`;
      html += `<td style="border:1px solid #ccc; padding:4px;">${item.sog.toFixed(2).toString().padStart(5, '0')}</td>`;
      html += `<td style="border:1px solid #ccc; padding:4px;">${cog.toString().padStart(3, '0')}°</td>`;
      html += `<td style="border:1px solid #ccc; padding:4px;">${epochToStrDate(item.lastupdate)}</td>`;
      html += '</tr>';
   }

   html += '</tbody></table></div>';

   // Show with Swal2
   Swal.fire({
     title: 'AIS Dump',
     html: html,
     width: '80%',
     confirmButtonText: 'Close'
   });
}

/**
 * Show AIS targets on the Windy map, replacing previous ones.
 * @param {Object[]} aisData - Array of AIS data objects.
 */
function showAIS(aisData) {
   if (aisLayer) {
      map.removeLayer(aisLayer);
   }

   aisLayer = L.layerGroup();

   for (const boat of aisData) {
      if (boat.name === "_Unsupported") continue;
      const rotation = boat.cog; // Heading in degrees
      const isMoving = boat.sog > 0;

      // Triangle icon via divIcon + CSS transform
      const icon = L.divIcon({
         className: '', // no default class
         html: `
            <div class="${isMoving ? 'ais-moving' : ''}" style="
            --cog: ${rotation}deg;
            width: 0; height: 0;
            border-left: 4px solid transparent;
            border-right: 4px solid transparent;
            border-bottom: 18px solid orange;
            transform: rotate(${rotation}deg);
            transform-origin: center;
            "></div>
            `,
         iconSize: [12, 12],
         iconAnchor: [6, 6]
      });

      const marker = L.marker([boat.lat, boat.lon], { icon });
      const cog = (boat.cog < 0) ? boat.cog + 360 : boat.cog;
      const popupContent = `
         <b>${boat.name}</b><br>
         Country: ${boat.country}<br>
         MinDist: ${boat.mindist} Kn<br>
         MMSI: ${boat.mmsi}<br>
         Coordinate: ${toDMSString (boat.lat, boat.lon)}<br>
         SOG: ${boat.sog.toFixed(2)} Kn<br>
         COG: ${cog}°<br>
         LastUpdate: ${epochToStrDate(boat.lastupdate)}
      `;

      marker.bindPopup(popupContent);
      aisLayer.addLayer(marker);
   }   
   aisLayer.addTo(map);
}

/**
 * Fetches the current AIS information from the local server.
 * Uses a timeout to prevent hanging if the server is unresponsive.
 */
function fetchAisPosition() {
   const controller = new AbortController();
   const timeout = setTimeout(() => {
      controller.abort(); // Cancel request after 5s
   }, 5000);

   fetch (aisUrl, {
      method: 'GET',
      signal: controller.signal
   })
   .then(response => {
      clearTimeout(timeout);
      console.log('Server response status:', response.status);
      if (!response.ok) {
         throw new Error(`HTTP error ${response.status}`);
      }
      return response.text(); // read as text first to see raw output
   })
   .then(text => {
      console.log('Raw response from server:', text);

      let data;
      try {
         data = JSON.parse(text);
      } catch (e) {
         console.error('Failed to parse JSON:', e.message);
         if (gpsMarker) map.removeLayer(gpsMarker);
         return;
      }

      console.log('Parsed JSON:', data);
      aisData = data;
      showAIS (data);
   })
   .catch(err => {
      clearTimeout(timeout);
      console.error('Fetch error:', err.message);
      if (gpsMarker) map.removeLayer(gpsMarker);
   });
}

