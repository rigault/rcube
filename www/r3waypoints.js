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

      let prevLat = competitor.lat, prevLon = competitor.lon;
      console.log ("N waypoints: "+ wayPoints.length);
      for (let i = 0; i < wayPoints.length; i++) {
         const strCoord = latLonToStr (wayPoints [i][0], wayPoints [i][1], DMSType);
         const orthoC = orthoCap(prevLat, prevLon, wayPoints [i][0], wayPoints [i][1]).toFixed(1);
         const orthoD = orthoDist(prevLat, prevLon, wayPoints [i][0], wayPoints [i][1]).toFixed(2); 
         console.log (orthoC, orthoD, prevLat, prevLon, wayPoints [i][0], wayPoints [i][1]);
         const loxoC = loxoCap(prevLat, prevLon, wayPoints [i][0], wayPoints [i][1]).toFixed(1);
         const loxoD = loxoDist(prevLat, prevLon, wayPoints [i][0], wayPoints [i][1]).toFixed(2);

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

         prevLat = wayPoints [i][0], prevLon = wayPoints [i][1];
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

