let marks = []; // global; may be later replaced with a Promise

/**
 * Fetch marks from the API and initialize marks
 *
 * Expects the server response to be an array of marks,
 * where each mark is an object {what, name, id, lat0, lon0,, lat1, lon1, status}
 *
 * @param {L.Map} map - Leaflet map instance.
 */
async function getMarks (map) {
   if (!map) throw new Error("Leaflet map instance is required.");
   let response;
   try {
      response = await fetch(apiUrl, {
         method: "POST",
         headers: {
            "Content-Type": "application/x-www-form-urlencoded; charset=UTF-8"
         },
         body: `type=${REQ.MARKS}`,
         cache: "no-store",
         credentials: "omit",
      });
   } catch (networkErr) {
      console.error("Network error while fetching marks:", networkErr);
      throw new Error("Network error while fetching polygons.");
   }

   if (!response.ok) {
      const msg = `Server error: ${response.status} ${response.statusText}`;
      console.error(msg);
      throw new Error(msg);
   }

   try {
      marks = await response.json(); // global variable
   } catch (parseErr) {
      console.error("Invalid JSON from server:", parseErr);
      throw new Error("Invalid JSON from server.");
   }

   // Basic validation and drawing
   if (!Array.isArray(marks)) {
      throw new Error("Unexpected response shape: expected an array of marks.");
   }
   console.log (JSON.stringify (marks, null, 2));
   displayMarks(map, marks);
}

/**
 * Render marks as a simple HTML table.
 * @param {MarkVR[]} marksArray
 * @param {number} decimals
 * @returns {string}
 */
function renderMarksTable(marksArray, decimals) {
   const fmt = (v) => (v === null || v === undefined || Number.isNaN(v)) ? '' : Number(v).toFixed(decimals);
   const esc = (s) => String(s ?? '').replace(/[&<>"']/g, m => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[m]));

   if (!Array.isArray(marksArray) || marksArray.length === 0) {
      return `<div style="padding:.5rem">No data.</div>`;
   }

   const rows = marksArray.map(m => `
      <tr>
         <td>${esc(m.what)}</td>
         <td>${esc(m.name)}</td>
         <td>${esc(m.id)}</td>
         <td style="text-align:right">${fmt(m.lat0)}</td>
         <td style="text-align:right">${fmt(m.lon0)}</td>
         <td style="text-align:right">${fmt(m.lat1)}</td>
         <td style="text-align:right">${fmt(m.lon1)}</td>
         <td>${esc(m.status)}</td>
      </tr>
   `).join('');

   return `
      <div style="max-height:60vh; overflow:auto; margin-top:0.5rem">
         <table style="width:100%; border-collapse:collapse">
            <thead>
               <tr>
                  <th style="text-align:left; border-bottom:1px solid #ddd; padding:4px">what</th>
                  <th style="text-align:left; border-bottom:1px solid #ddd; padding:4px">name</th>
                  <th style="text-align:left; border-bottom:1px solid #ddd; padding:4px">id</th>
                  <th style="text-align:right; border-bottom:1px solid #ddd; padding:4px">lat0</th>
                  <th style="text-align:right; border-bottom:1px solid #ddd; padding:4px">lon0</th>
                  <th style="text-align:right; border-bottom:1px solid #ddd; padding:4px">lat1</th>
                  <th style="text-align:right; border-bottom:1px solid #ddd; padding:4px">lon1</th>
                  <th style="text-align:left; border-bottom:1px solid #ddd; padding:4px">status</th>
               </tr>
            </thead>
            <tbody>
               ${rows}
            </tbody>
         </table>
      </div>
   `;
}

/**
 * Show the marks array in a SweetAlert2 modal as a table.
 * @param {MarkVR[]} marks
 */
function showMarks(marks) {
   const title = 'VR marks';
   const decimals = 6;

   if (! Array.isArray(marks) || marks.length === 0) return;

   Swal.fire({
      title,
      html: renderMarksTable(marks, decimals),
      width: Math.min(window.innerWidth * 0.95, 1000),
      showCloseButton: true,
      confirmButtonText: 'Close',
      customClass: { htmlContainer: 'swal2-overflow' }
   });
}

/**
 * Return the symbol (emoji) to use for a given `what`.
 * Falls back to 'x' for Invisible/unknown.
 * @param {string} what
 * @returns {string}
 */
function symbolFromWhat(what) {
   const lower = (what || '').trim().toLowerCase();
   if (lower.includes('start')) return '🚩';
   if (lower.includes('buoy')) return '🚩';
   if (lower.includes('end')) return '🏁';
   if (lower.includes('invisible')) return 'x';
   if (lower.includes('gate')) return 'G';
   return 'x';
}

/**
 * Check if a name is non-empty after trimming.
 * @param {string} name
 * @returns {boolean}
 */
function hasName(name) {
   return typeof name === 'string' && name.trim().length > 0;
}

/**
 * Basic HTML escape for popup content.
 * @param {string} s
 * @returns {string}
 */
function escHtml(s) {
   return String(s ?? '').replace(/[&<>"']/g, m => (
      {'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[m]
   ));
}

/**
 * Add emoji markers for marksVR onto a Leaflet map.
 * - Places at (lat0, lon0)
 * - Shows ONLY the symbol on map (no tooltip)
 * - On click, opens a popup with the `name` (if present)
 *
 * Uses/replaces map._marksLayer to allow redraws.
 *
 * @param {L.Map} map
 * @param {array} marks
 */
function displayMarks(map, marks) {
   if (map._marksLayer) {
      map.removeLayer(map._marksLayer);
      map._marksLayer = undefined;
   }
   if (!marks || marks.length === 0) {
      console.warn('displayMarks: no data to display');
      return;
   }

   const group = L.layerGroup();
   map._marksLayer = group;
   group.addTo(map);
   
   for (const m of marks) {
      const lat = (typeof m.lat0 === 'number') ? m.lat0 : null;
      const lon = (typeof m.lon0 === 'number') ? m.lon0 : null;
      if (lat === null || lon === null || Number.isNaN(lat) || Number.isNaN(lon)) {
         continue;
      }

      const sym = symbolFromWhat(m.what);
      const icon = L.divIcon({
         className: 'mark-emoji',
         html: sym,
         iconSize: [22, 22],
         iconAnchor: [11, 11]
      });

      const marker = L.marker([lat, lon], { icon });
      marker.addTo(group);

      if (hasName(m.name)) {
         marker.bindPopup(`<strong>${escHtml(m.name.trim())}</strong>`, {
            closeButton: true,
            autoClose: true
         });
      }
   }
}


