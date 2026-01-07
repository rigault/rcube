async function manageWaypoints () {
  orthoRouteGroup?.clearLayers();
  if (destination) destination.remove ();
  await manageWaypointsBox (myWayPoints);
  saveAppState();
  showWayPoint (myWayPoints);
}

async function manageWaypointsBox(wayPoints) {
  // Make a working copy so Cancel doesn't mutate original
  const wp = wayPoints.map(p => [Number(p[0]), Number(p[1])]);

  const isFiniteNumber = (x) => Number.isFinite(x);

  const buildHtml = () => {
    const rows = wp.map((p, i) => {
      const isDest = (i === wp.length - 1);
      const name = isDest ? "dest" : `wp${i}`;
      const coordStr = latLonToStr(p[0], p[1], DMSType);

      return `
        <tr data-idx="${i}">
          <td style="white-space:nowrap;"><b>${esc(name)}</b></td>
          <td>
            <input class="swal2-input wp-coords" style="width:100%; margin:0;"
                   value="${esc(coordStr)}"
                   placeholder="LAT - LON (DMS/DM)"/>
          </td>
          <td style="white-space:nowrap;">
            <button type="button" class="swal2-styled wp-up"   style="padding:.35em .7em;">↑</button>
            <button type="button" class="swal2-styled wp-down" style="padding:.35em .7em;">↓</button>
            <button type="button" class="swal2-styled wp-del"  style="padding:.35em .7em;">✖</button>
          </td>
        </tr>
      `;
    }).join("");

    return `
      <div style="text-align:left;">
        <div style="margin:0 0 .75em 0;">
          <button type="button" id="wpAdd" class="swal2-styled" style="padding:.35em .7em;">+ Add waypoint</button>
        </div>

        <table style="width:100%; border-collapse:collapse;">
          <thead>
            <tr>
              <th style="text-align:left; padding:.25em;">Name</th>
              <th style="text-align:left; padding:.25em;">Coordinates</th>
              <th style="text-align:left; padding:.25em;">Actions</th>
            </tr>
          </thead>
          <tbody id="wpTbody">
            ${rows}
          </tbody>
        </table>

        <div style="margin-top:.75em; font-size:.9em; opacity:.85;">
          Expected form: <code>lat - lon</code> (ex: <code>48°51.000' N - 002°21.000' E</code>)
        </div>
      </div>
    `;
  };

  const parseCoords = (coords) => {
    // User provided string, must contain " - "
    const parts = coords.split(" - ");
    if (parts.length !== 2) return null;

    let [latDMS, lonDMS] = parts;
    latDMS = latDMS.trim();
    lonDMS = lonDMS.trim();

    const lat = dmsToDecimal(latDMS);
    const lon = dmsToDecimal(lonDMS);

    if (!isFiniteNumber(lat) || !isFiniteNumber(lon)) return null;
    if (lat < -90 || lat > 90) return null;
    if (lon < -180 || lon > 180) return null;

    return [lat, lon];
  };

  const commitFromInputs = () => {
    const tbody = document.getElementById("wpTbody");
    const trs = Array.from(tbody.querySelectorAll("tr"));

    for (let r = 0; r < trs.length; r++) {
      const input = trs[r].querySelector("input.wp-coords");
      const parsed = parseCoords(input.value);

      if (!parsed) {
        // Build a helpful error message with the row name
        const isDest = (r === trs.length - 1);
        const name = isDest ? "dest" : `wp${r + 1}`;
        return `Invalid coordinates for ${name}. Expected: "lat - lon".`;
      }
      wp[r] = parsed;
    }
    return null; // ok
  };

  const refresh = () => {
    // Re-render the modal content and re-bind handlers
    Swal.update({ html: buildHtml() });
    bindHandlers();
  };

  const bindHandlers = () => {
    const addBtn = document.getElementById("wpAdd");
    const tbody = document.getElementById("wpTbody");
    if (!tbody) return;

    addBtn?.addEventListener("click", () => {
      // Add before dest if possible, else just add
      if (wp.length >= 1) {
        // Insert new wp just before last element (dest)
        const dest = wp[wp.length - 1];
        wp.splice(wp.length - 1, 0, [...dest]); // duplicate dest as new wp by default
      } else {
        wp.push([0, 0]);
      }
      refresh();
    });

    tbody.addEventListener("click", (e) => {
      const btn = e.target.closest("button");
      if (!btn) return;

      const tr = e.target.closest("tr");
      if (!tr) return;
      const idx = Number(tr.getAttribute("data-idx"));

      if (btn.classList.contains("wp-del")) {
        if (wp.length <= 1) return; // keep at least one point (dest)
        wp.splice(idx, 1);
        refresh();
        return;
      }

      if (btn.classList.contains("wp-up")) {
        if (idx <= 0) return;
        const tmp = wp[idx - 1];
        wp[idx - 1] = wp[idx];
        wp[idx] = tmp;
        refresh();
        return;
      }

      if (btn.classList.contains("wp-down")) {
        if (idx >= wp.length - 1) return;
        const tmp = wp[idx + 1];
        wp[idx + 1] = wp[idx];
        wp[idx] = tmp;
        refresh();
        return;
      }
    });
  };

  const result = await Swal.fire({
    title: "Manage waypoints",
    html: buildHtml(),
    width: 900,
    showCancelButton: true,
    confirmButtonText: "OK",
    cancelButtonText: "Cancel",
    focusConfirm: false,
    didOpen: () => bindHandlers(),
    preConfirm: () => {
      const err = commitFromInputs();
      if (err) {
        Swal.showValidationMessage(err);
        return false;
      }
      return true;
    }
  });

  if (!result.isConfirmed) return false;

  // Commit back to original array (mutate in place)
  wayPoints.length = 0;
  for (const p of wp) wayPoints.push([p[0], p[1]]);
  return true;
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
function displayWaypoints(competitors, wayPoints) {
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

