const EARTH_RADIUS_NM = 3440.065; // Rayon moyen de la Terre en milles nautiques
const DEG2RAD = Math.PI / 180;
const RAD2DEG = 180 / Math.PI;

/**
 * Computes the initial orthodromic (great-circle) bearing from one point to another.
 *
 * This function calculates the initial heading (bearing) in degrees required 
 * to travel from the first point (`prev`) to the second (`curr`) along a great-circle route.
 * 
 * @param {Array<number>} prev - The starting point as an array [latitude, longitude] in degrees.
 * @param {Array<number>} curr - The destination point as an array [latitude, longitude] in degrees.
 * @returns {number} The initial bearing in degrees, ranging from 0 to 360°.
 */
function orthoCap (prev, curr) {
   if ((curr === undefined) || (prev === undefined)) return 0;
   // Conversion des degrés en radians
   const toRadians = deg => deg * Math.PI / 180;
   const toDegrees = rad => rad * 180 / Math.PI;
    
   const lat1 = toRadians(prev[0]);
   const lon1 = toRadians(prev[1]);
   const lat2 = toRadians(curr[0]);
   const lon2 = toRadians(curr[1]);

   const dLon = lon2 - lon1;

   // Formule de calcul du cap initial (bearing) en radians
   const y = Math.sin(dLon) * Math.cos(lat2);
   const x = Math.cos(lat1) * Math.sin(lat2) - Math.sin(lat1) * Math.cos(lat2) * Math.cos(dLon);
   const heading = Math.atan2(y, x);

   return (toDegrees (heading) + 360) % 360;
}

/**
 * Calculates the orthodromic (great-circle) distance between two points.
 *
 * @param {Array<number>} prev - Starting point [latitude, longitude] in degrees.
 * @param {Array<number>} curr - Destination point [latitude, longitude] in degrees.
 * @returns {number} Distance in nautical miles.
 */
function orthoDist (prev, curr) {
  if ((curr === undefined) || (prev === undefined)) return 0;
  const [lat1, lon1] = prev.map(deg => deg * DEG2RAD);
  const [lat2, lon2] = curr.map(deg => deg * DEG2RAD);

  const deltaSigma = Math.acos(
    Math.sin(lat1) * Math.sin(lat2) +
    Math.cos(lat1) * Math.cos(lat2) * Math.cos(lon2 - lon1)
  );

  return EARTH_RADIUS_NM * deltaSigma;
}

/**
 * Computes the initial loxodromic (rhumb line) bearing from one point to another.
 *
 * @param {Array<number>} prev - Starting point [latitude, longitude] in degrees.
 * @param {Array<number>} curr - Destination point [latitude, longitude] in degrees.
 * @returns {number} Initial bearing in degrees from 0 to 360.
 */
function loxoCap (prev, curr) {
  if ((curr === undefined) || (prev === undefined)) return 0;
  const [lat1, lon1] = prev.map(deg => deg * DEG2RAD);
  const [lat2, lon2] = curr.map(deg => deg * DEG2RAD);
  let dLon = lon2 - lon1;

  const dPhi = Math.log(Math.tan(Math.PI / 4 + lat2 / 2) / Math.tan(Math.PI / 4 + lat1 / 2));

  // Corriger dLon pour franchissement de l'antiméridien
  if (Math.abs(dLon) > Math.PI) {
    dLon = dLon > 0 ? -(2 * Math.PI - dLon) : (2 * Math.PI + dLon);
  }

  let bearing = Math.atan2(dLon, dPhi) * RAD2DEG;
  return (bearing + 360) % 360;
}

/**
 * Calculates the loxodromic (rhumb line) distance between two points.
 *
 * @param {Array<number>} prev - Starting point [latitude, longitude] in degrees.
 * @param {Array<number>} curr - Destination point [latitude, longitude] in degrees.
 * @returns {number} Distance in nautical miles.
 */
function loxoDist (prev, curr) {
  if ((curr === undefined) || (prev === undefined)) return 0;
  const [lat1, lon1] = prev.map(deg => deg * DEG2RAD);
  const [lat2, lon2] = curr.map(deg => deg * DEG2RAD);
  let dLon = Math.abs(lon2 - lon1);
  const dLat = lat2 - lat1;

  const dPhi = Math.log(Math.tan(Math.PI / 4 + lat2 / 2) / Math.tan(Math.PI / 4 + lat1 / 2));

  const q = Math.abs(dPhi) > 1e-12 ? dLat / dPhi : Math.cos(lat1);

  // Corriger dLon pour franchissement de l'antiméridien
  if (dLon > Math.PI) {
    dLon = 2 * Math.PI - dLon;
  }

  const distance = Math.sqrt(dLat * dLat + q * q * dLon * dLon);
  return EARTH_RADIUS_NM * distance;
}

/**
 * Displays waypoints and computed distances/courses for each competitor.
 *
 * For each competitor, this function builds a table that includes, for each leg between waypoints:
 * the index, lat/lon, orthodromic course and distance, and loxodromic course and distance.
 * Totals for distances are shown at the bottom in bold red.
 *
 * @param {Array<Object>} competitors - Array of competitors, each having at least `name`, `lat`, `lon`.
 * @param {Array<Array<number>>} wayPoints - Array of waypoints, each being [lat, lon].
 */
function displayWayPoints(competitors, wayPoints) {
   if (!Array.isArray(wayPoints) || wayPoints.length === 0) {
      Swal.fire({
         icon: "warning",
         title: "No waypoint defined",
         text: "Please define at least one waypoint before displaying data.",
      });
      return;
   }

   const container = document.createElement("div");

   competitors.forEach((competitor, index) => {
      const wrapper = document.createElement("div");
      wrapper.style.border = "1px solid #ccc";
      wrapper.style.borderRadius = "8px";
      wrapper.style.padding = "10px";
      wrapper.style.margin = "0 10px 20px 10px"; // marges gauche/droite
      wrapper.style.backgroundColor = "#f9f9f9";

      const table = document.createElement("table");
      table.style.borderCollapse = "collapse";
      table.style.width = "100%";
      table.style.textAlign = "right"; // alignement des nombres à droite
      table.style.margin = "auto";

      const caption = document.createElement("caption");
      caption.textContent = competitor.name;
      caption.style.fontWeight = "bold";
      caption.style.fontSize = "1.2em";
      caption.style.marginBottom = "0.5em";
      table.appendChild(caption);

      const thead = document.createElement("thead");
      thead.innerHTML = `
         <tr>
            <th style="text-align:left;">Waypoint</th>
            <th>Coord.</th>
            <th>Ortho Cap (°)</th>
            <th>Ortho Dist (NM)</th>
            <th>Loxo Cap (°)</th>
            <th>Loxo Dist (NM)</th>
         </tr>`;
      table.appendChild(thead);

      const tbody = document.createElement("tbody");

      let totalOrtho = 0;
      let totalLoxo = 0;
      let copyText = "Waypoint\tCoord.\tOrtho Cap (°)\tOrtho Dist (NM)\tLoxo Cap (°)\tLoxo Dist (NM)\n";

      let prev = [competitor.lat, competitor.lon];
      for (let i = 0; i < wayPoints.length; i++) {
         const curr = wayPoints[i];
         const strCoord = toDMSString (curr [0], curr [1]);
         const orthoC = orthoCap(prev, curr).toFixed(1);
         const orthoD = orthoDist(prev, curr).toFixed(2);
         const loxoC = loxoCap(prev, curr).toFixed(1);
         const loxoD = loxoDist(prev, curr).toFixed(2);

         totalOrtho += parseFloat(orthoD);
         totalLoxo += parseFloat(loxoD);

         const label = (i === wayPoints.length - 1) ? "Dest." : i;
         const row = document.createElement("tr");
         row.innerHTML = `
            <td style="text-align:left;">${label}</td>
            <td>${strCoord}</td>
            <td>${orthoC}</td>
            <td>${orthoD}</td>
            <td>${loxoC}</td>
            <td>${loxoD}</td>`;
         tbody.appendChild(row);

         copyText += `${label}\t${strCoord}\t${orthoC}\t${orthoD}\t${loxoC}\t${loxoD}\n`;

         prev = curr;
      }

      // Totals row
      const totalRow = document.createElement("tr");
      totalRow.innerHTML = `
         <td style="font-weight:bold; color:red; text-align:left;">Total Distance</td>
         <td></td>
         <td></td>
         <td style="font-weight:bold; color:red;">${totalOrtho.toFixed(2)}</td>
         <td></td>
         <td style="font-weight:bold; color:red;">${totalLoxo.toFixed(2)}</td>`;
      tbody.appendChild(totalRow);

      copyText += `Total Distance\t\t\t${totalOrtho.toFixed(2)}\t\t${totalLoxo.toFixed(2)}\n`;

      table.appendChild(tbody);
      wrapper.appendChild(table);

      // Copy button
      const copyButton = document.createElement("button");
      copyButton.textContent = "Copy Table";
      copyButton.style.marginTop = "10px";
      copyButton.style.padding = "6px 12px";
      copyButton.style.border = "1px solid #999";
      copyButton.style.borderRadius = "4px";
      copyButton.style.backgroundColor = "#eee";
      copyButton.style.cursor = "pointer";
      copyButton.addEventListener("click", () => {
         navigator.clipboard.writeText(copyText).then(() => {
            Swal.fire({
               toast: true,
               position: 'top-end',
               icon: 'success',
               title: `Table copied for ${competitor.name}`,
               showConfirmButton: false,
               timer: 1500
            });
         });
      });

      wrapper.appendChild(copyButton);
      container.appendChild(wrapper);
   });

   Swal.fire({
      title: "Waypoint Distances",
       html: container,
      width: "90%",
      scrollbarPadding: false,
      confirmButtonText: "Close"
   });
}

