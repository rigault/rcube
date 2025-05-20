/* jshint esversion: 6 */

/**
 * Updates the boat selection logic after competitors are added/removed.
 * Adjusts routeParam.iBoat accordingly to ensure a valid selection.
 */
function updateBoatSelect() {
   const boatNames = competitors.map(c => c.name);

   if (boatNames.length === 0) {
      routeParam.iBoat = 0;
      return;
   }
   if (boatNames.length === 1) {
      routeParam.iBoat = 1;
      return;
   }
   const maxBoatIndex = competitors.length;
   if (routeParam.iBoat > maxBoatIndex) {
      routeParam.iBoat = 1;
   }
}

/**
 * Displays a modal to manage a list of competitors, allowing editing, adding, and removing entries.
 * @param {Array} competitors - An array of competitors, where each entry is [name, lat, lon, colorIndex].
 */
async function manageCompetitors(competitors) {
   let palette = "<div style='display: flex; align-items: center; gap: 10px; margin-left: 20px'>";
   colorMap.forEach((color, index) => {
      palette += `<span style="display: flex; align-items: center; gap: 5px;">
                    <div style="width: 10px; height: 10px; border-radius: 50%; background-color: ${color}; border: 1px solid #333;"></div>
                    ${index}
                  </span>`;
   });
   palette += "</div>";

   let html = `<table style='width:100%; text-align:left; table-layout: auto;'>
               <tr><th style='width:25%'>Name</th><th style='width:10%'>Color</th><th style='width:55%'>Coordinates</th><th style='width:10%'>Action</th></tr>`;
   competitors.forEach((c, index) => {
      html += `<tr>
               <td><input type='text' style='width:100%' value='${c.name}' id='name${index}'></td>
               <td><input type='number' style='width:100%' value='${c.color}' id='color${index}' min='0' max='${colorMap.length - 1}'></td>
               <td><input type='text' style='width:100%; font-size:14px;' value='${latLonToStr(c.lat, c.lon)}' id='coord${index}'></td>
               <td>${competitors.length > 1 ? `<button type='button' onclick='removeCompetitor(${index})'>❌</button>` : ''}</td>
            </tr>`;
   });
   html += `</table>`;
   html += `<input type="file" id="csvInput" accept=".csv" style="display:none">
            <button onclick="document.getElementById('csvInput').click()">Import</button>`;

   html += palette;

   await Swal.fire({
      title: 'Manage Competitors',
      width: '800px',
      html: `<div style='width: 100%; overflow-x: auto;'>${html}</div>`,
      showCancelButton: true,
      confirmButtonText: 'Save',

      didOpen: () => {
         const input = document.getElementById("csvInput");
         if (!input) {
            console.error("❌ csvInput not found !");
            return;
         }
         input.addEventListener("change", () => importCSV(input.files[0], competitors));
      },

      preConfirm: () => {
         let updatedCompetitors = [];
         for (let index = 0; index < competitors.length; index++) {
            let name = document.getElementById(`name${index}`).value.trim();
            let color = parseInt(document.getElementById(`color${index}`).value);
            let coords = document.getElementById(`coord${index}`).value.trim();
            if (!coords.includes(' - ')) {
               Swal.showValidationMessage('Invalid coordinate format (expected: lat - lon)');
               return false;
            }
            let [latDMS, lonDMS] = coords.split(' - ');
            let lat = dmsToDecimal(latDMS.trim());
            let lon = dmsToDecimal(lonDMS.trim());
            updatedCompetitors.push({
               name,
               lat,
               lon,
               color,
               marker: competitors[index].marker || {} // conserver l'objet marker si présent
            });
         }

         // Mise à jour du tableau d'origine (référence conservée)
         competitors.length = 0;
         competitors.push(...updatedCompetitors);
         saveAppState ();
         updateBoatSelect();
         orthoRouteGroup.clearLayers();
         //iComp = 0;
         //let competitor = competitors [iComp];
         for (let competitor of competitors) { 
            competitor.marker.setLatLng ([competitor.lat, competitor.lon]); // Move the mark
            drawOrtho (competitor, myWayPoints);
            if (myWayPoints.length > 0) {
               let heading = orthoCap([competitor.lat, competitor.lon], myWayPoints [0]);
               competitor.marker._icon.setAttribute ('data-heading', heading);
               updateIconStyle (competitor.marker);
            }
            competitor.marker.bindPopup (popup4Comp (competitor));
         }
         clearRoutes ();
         // boatName = competitors [0].name;
         if (myWayPoints.length > 0)
            showDestination (myWayPoints [myWayPoints.length - 1][0], myWayPoints [myWayPoints.length - 1][1]); // last element
         
       },
   });
   //const input = document.getElementById ("csvInput");
   //input?.addEventListener("change", () => importCSV(input.files[0], competitors));
}

/**
 * Removes a competitor from the list, ensuring at least one remains.
 * @param {number} index - The index of the competitor to remove.
 */
function removeCompetitor (index) {
   if (competitors.length > 1) {
      competitors [index].marker.remove ();
      competitors.splice(index, 1);
      updateBoatSelect();
      manageCompetitors(competitors);
   }
}

/**
 * Imports competitor positions from a Virtual Regatta Dashboard CSV file.
 *
 * - Matches competitors from the given list by name (fuzzy match).
 * - Extracts coordinates (lat/lon) and other sailing data (TWS, speed, sail, etc.).
 * - Displays a confirmation table in a SweetAlert2 popup.
 * - If confirmed, updates the corresponding competitors' `lat` and `lon`.
 *
 * @param {File} file - The CSV file selected by the user.
 * @param {Array} competitors - The current list of competitors to update.
 */
function importCSV (file, competitors) {
   if (!file) return;

   const reader = new FileReader();
   reader.onload = function (e) {
      const lines = e.target.result.split("\n");
      const matching = [];

      for (let line of lines) {
         if (line.includes("°") || line.includes("d")) {
            const fields = line.split(";").map(f => f.trim());
            const skipper = fields[1];
            const nameKey = skipper?.toLowerCase().replace(/[^a-z0-9]/gi, "");
            const posMatch = line.match(/(\d+)°(\d+)'([\d.]+)"([NS])\s*-\s*(\d+)°(\d+)'([\d.]+)"([EW])/);
            if (!posMatch) continue;

            let lat = + posMatch[1] + posMatch[2] / 60 + posMatch[3] / 3600;
            let lon = + posMatch[5] + posMatch[6] / 60 + posMatch[7] / 3600;
            if (posMatch[4] === "S") lat = -lat;
            if (posMatch[8] === "W") lon = -lon;

            const comp = competitors.find (c =>
               nameKey.includes(c.name.toLowerCase().replace(/[^a-z0-9]/gi, ""))
            );

            if (comp) {
               matching.push({
                  name: comp.name, lat, lon,
                  rank: fields[3], DTF: fields[4], DTU: fields[5],
                  sail: fields[7], hdg: fields[11], twa: fields[12],
                  tws: fields[13], speed: fields[14], factor: fields[15],
                  foils: fields[16], options: fields[17], team: fields[18]
               });
            }
         }
      }

      if (matching.length === 0) {
         Swal.fire ("No competitor found", "", "info");
         return;
       }

       let html = `<table border="1" style="width:100%; font-size:12px; text-align:center;"><thead><tr>
          <th>Nom</th><th>Coord</th><th>Rank</th><th>DTF</th><th>DTU</th>
          <th>Sail</th><th>HDG</th><th>TWA</th><th>TWS</th><th>Speed</th>
          <th>Factor</th><th>Foils</th><th>Option</th><th>Team</th>
          </tr></thead><tbody>`;

       for (const c of matching) {
         html += `<tr>
           <td>${c.name}</td><td>${toDMSString (c.lat, c.lon)}</td>
           <td>${c.rank}</td><td>${c.DTF}</td><td>${c.DTU}</td><td>${c.sail}</td>
           <td>${c.hdg}</td><td>${c.twa}</td><td>${c.tws}</td><td>${c.speed}</td>
           <td>${c.factor}</td><td>${c.foils}</td><td>${c.options}</td><td>${c.team}</td>
          </tr>`;
       }
       html += `</tbody></table>`;

       Swal.fire({
          title: `${matching.length} competitors match`,
          html,
          width: "90%",
          showCancelButton: true,
          confirmButtonText: "Update"
       }).then(result => {
          if (result.isConfirmed) {
             matching.forEach(m => {
                const c = competitors.find(c => c.name === m.name);
                if (c) {
                   c.lat = m.lat;
                   c.lon = m.lon;
                }
             });

             // 🔁 relaunch  manageCompetitors()
             manageCompetitors(competitors);
         }
      });
   };
   reader.readAsText(file);
}

/**
 * Displays a table to compare competitors.
 * @param {Array} - result - The table with duration info.
 */
function dispAllCompetitors (result) {
   if (!result || result.length === 0) {
      Swal.fire({
         icon: 'warning',
         title: 'No competitor',
         text: 'No data available to compare',
      });
      return;
   }
   let bestTime = Math.min (...result); // Find best duration 
   let minDuration = formatDuration (bestTime);
   let ETA;
   let duration;

   // Meta data build
   let metaData = `
      <p><strong>Number of competitors:</strong> ${competitors.length}</p>
      <p><strong>Start date and time:</strong> ${dateToStr(routeParam.startTime)}</p>
      <p><strong>Isochrone Time Step:</strong> ${routeParam.isoStep} sec</p>
      <p><strong>Polar:</strong> ${routeParam.polar}</p>
      <p><strong>Best Duration:</strong> ${minDuration}</p>
      
   `;

   // Competitors comparison table
   let table = `<table class="comp-table">
      <thead>
         <tr>
            <th>Name</th>
            <th>Coord.</th>
            <th>Duration</th>
            <th>ETA</th>
         </tr>
      </thead>
      <tbody>
   `;

   competitors.forEach((comp, index) => {
      if (result[index] === -1) duration = ETA = "NA";
      else {
         duration = formatDuration (result[index]);
         let newDate = new Date (routeParam.startTime.getTime () + result [index] * 1000);
         ETA = dateToStr (newDate);
      }         
      table += `
         <tr>
            <td>${comp.name}</td>
            <td>${latLonToStr(comp.lat, comp.lon)}</td>
            <td>${duration}</td>
            <td>${ETA}</td>
         </tr>
      `;
   });

   table += `</tbody></table>`;

   // Affichage avec SweetAlert2
   Swal.fire({
      title: 'Competitors benchmark',
      html: metaData + table,
      width: '80%',
      confirmButtonText: 'Close',
   });
}

