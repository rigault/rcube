/* jshint esversion: 6 */

const POL_TYPE = {WIND_POLAR: 0, WAVE_POLAR: 1}; 

/**
 * Provide information about polar file 
 * @param {string} polarName - name of polar
 */
function polarInfo (polType, polarName) {
   const dir = (polType === POL_TYPE.WIND_POLAR) ? "pol" : "wavepol";
   const formData = `type=${REQ.POLAR}&polar=${dir}/${polarName}`;
   console.log ("In polarInfo:" + formData);

   fetch (apiUrl, {
      method: "POST",
      headers: {
         "Content-Type": "application/x-www-form-urlencoded"
      },
      body: formData
   })
   .then(response => response.ok ? response.json() : Promise.reject (`In polarInfo Error ${response.status}: ${response.statusText}`))
   .then (data => {
      if (Array.isArray(data.array)) {
         generatePolarPlotly (polType, polarName, data); // polar and sailPolar and legend
      } else {
         throw new Error ("Invalid format");
      }
   })
   .catch (error => {
      Swal.fire({
         title: "Erreur",
         text: error,
         icon: "error",
         confirmButtonColor: "#FFA500",
         confirmButtonText: "OK"
      });
   });
}

/**
 * dump polar information about polar
 * @param {Object} data - polar information
 */
function showPolarTable (polType, polarName, data) {
   let windSpeeds = data.array[0].slice(1).map(v => parseFloat(v)).filter(v => !isNaN(v));
   console.log("data: " + JSON.stringify(data));

   // Vérifier si sailData et legend sont valides
   let sailDataValid = data && data.arraySail && Array.isArray(data.arraySail) && data.arraySail.length > 0;
   let legendValid = data.legend && Array.isArray(data.legend);

   if (!sailDataValid) {
      console.warn ("sailData is empty or undefined. No color.");
   }
   if (!legendValid) {
      console.warn ("legend is empty or undefined, No legend.");
   }

   let tableHTML = "<table border='1' style='border-collapse: collapse; width: 100%; text-align: center; font-size: 8px'>";

   // Ligne d'en-tête (TWS for wind, height for Waves)
   tableHTML += (polType === POL_TYPE.WIND_POLAR) ? "<tr style='font-weight: bold; background-color: #FFA500; color: white;'><th>TWA / TWS</th>"
                                                 : "<tr style='font-weight: bold; background-color: #FFA500; color: white;'><th>Angle / Height</th>";
   windSpeeds.forEach(tws => {
      tableHTML += `<th>${tws}</th>`;
   });
   tableHTML += "</tr>";
   //const sailNames = Object.keys(sailLegend);
   const sailNames = data.legend;
   console.log("sailNames: " + JSON.stringify(sailNames));

   // Création du tableau principal
   data.array.slice(1).forEach((row, rowIndex) => {
      let twa = row[0]; // Angle du vent (TWA)
      tableHTML += `<tr><td style='font-weight: bold; background-color: #f0f0f0;'>${twa}°</td>`;

      row.slice(1).forEach((value, colIndex) => {
         let cellStyle = "padding: 5px;"; // Style de base

         if (sailDataValid) {
            let sailValue = data.arraySail[rowIndex + 1]?.[colIndex + 1] || 0;
            const sailName = sailNames[sailValue];
            const entry = sailLegend[sailName] || { bg: 'lightgray', luminance: 200 };
            const textColor = getTextColorFromLuminance(entry.luminance);
            cellStyle += `background-color: ${entry.bg};color: ${textColor};font-weight: bold;`;
         }
         tableHTML += `<td style='${cellStyle}'>${value.toFixed(2)}</td>`;
      });

      tableHTML += "</tr>";
   });

   tableHTML += "</table>";

   // Génération HTML de la légende
   let legendHTML = "";
   if (legendValid) {
      legendHTML = "<div style='display: flex; justify-content: center; margin-top: 20px; flex-wrap: wrap;'>";
      data.legend.forEach(name => {
         const { bg, luminance } = sailLegend[name] ?? sailLegend.NA;
         const textColor = getTextColorFromLuminance(luminance);
         legendHTML += `
            <div style='
               background-color: ${bg};
               color: ${textColor};
               font-weight: bold;
               padding: 10px 15px;
               margin: 5px;
               border-radius: 5px;
               min-width: 50px;
               text-align: center;
               '>${name}</div>`;
      });
      legendHTML += "</div>";
   }

   // Affichage avec SweetAlert
   Swal.fire({
      title: (polType === POL_TYPE.WIND_POLAR) ? `Speed in Knots Max: ${data.max}` : `Height in meters Max: ${data.max}`,
      html: tableHTML + legendHTML,
      width: "100%",
      showCancelButton: true,
      confirmButtonText: 'Back',
      cancelButtonText: 'Cancel',     
      footer: `polarName: ${polarName}, nCol: ${data.nCol}, 
         nLine: ${data.nLine}, max: ${data.max}, nSail: ${data.nSail}, fromJson: ${data.fromJson}`,
   }).then((result) => {
      if (result.isConfirmed) {
         generatePolarPlotly (polType, polarName, data);
      }
   })
}

/** interpolate */
function interpolate (value, x0, y0, x1, y1) {
   return x1 === x0 ? y0 : y0 + ((value - x0) * (y1 - y0)) / (x1 - x0);
}

/** interpolate speeds */
function interpolateSpeeds (tws, windSpeeds, data) {
   if (tws <= windSpeeds[0]) return data.array.slice(1).map(row => parseFloat(row[1]));
   if (tws >= windSpeeds[windSpeeds.length - 1]) return data.array.slice(1).map(row => parseFloat(row[windSpeeds.length]));

   let i = 0;
   while (i < windSpeeds.length - 1 && windSpeeds[i + 1] < tws) i++;

   let speeds0 = data.array.slice(1).map(row => parseFloat(row[i + 1]));
   let speeds1 = data.array.slice(1).map(row => parseFloat(row[i + 2]));

   return speeds0.map((s0, index) => interpolate(tws, windSpeeds[i], s0, windSpeeds[i + 1], speeds1[index]));
}

/** Symetries polar data */
function symmetrizeData(twaValues, speeds) {
   let fullTwa = [...twaValues];
   let fullSpeeds = [...speeds];

   for (let i = twaValues.length - 2; i >= 0; i--) {
      fullTwa.push(360 - twaValues[i]);
      fullSpeeds.push(speeds[i]);
   }

   return { fullTwa, fullSpeeds };
}

/** calculate max and VMG values */ 
function findMaxSpeed (speeds) {
   return Math.max(...speeds);
}

/** calculate best VMG (Velocity Made Good) when wind is front */ 
function bestVmg (twaValues, speeds) {
   let bestSpeed = -1, bestAngle = -1;
   for (let i = 0; i < twaValues.length; i++) {
      if (twaValues[i] > 90) break;
      let vmg = speeds[i] * Math.cos(twaValues[i] * Math.PI / 180);
      if (vmg > bestSpeed) {
         bestSpeed = vmg;
         bestAngle = twaValues[i];
      }
   }
   return { angle: bestAngle, speed: bestSpeed };
}

/** calculate best VMG (Velocity Made Good) when wind is back */ 
function bestVmgBack(twaValues, speeds) {
   let bestSpeed = -1, bestAngle = -1;
   for (let i = 0; i < twaValues.length; i++) {
      if (twaValues[i] < 90) continue;
      let vmg = Math.abs(speeds[i] * Math.cos(twaValues[i] * Math.PI / 180));
      if (vmg > bestSpeed) {
         bestSpeed = vmg;
         bestAngle = twaValues[i];
      }
   }
   return { angle: bestAngle, speed: bestSpeed };
}

/*! provide raw informations in header if exist */
function moreInfoAboutPol(polType, polarName, data) {
   const header = data?.header;
   const lab = header.label ?? "(no label)";
   const pretty = JSON.stringify(header, null, 2); // ← indenté

   Swal.fire({
     title: `Additional information for: ${lab}`,
     width: '60%',
     showCancelButton: true,
     confirmButtonText: 'Back',
     cancelButtonText: 'Cancel',
     footer: `polarName: ${polarName}, nCol: ${data.nCol}, 
         nLine: ${data.nLine}, max: ${data.max}, nSail: ${data.nSail}, fromJson: ${data.fromJson}`,
     html: `
       <pre style="
         text-align:left; 
         white-space:pre; 
         margin:0; 
         max-height:60vh; 
         overflow:auto; 
         font-family: ui-monospace, SFMono-Regular, Menlo, monospace;
         font-size: 12px;
         line-height: 1.35;
       ">${pretty}</pre>
     `
   }).then((result) => {
      if (result.isConfirmed) {
         generatePolarPlotly (polType, polarName, data);
      }
   })
}

/**
 * draw with plotty information about polar 
 * @param {Object} data - polar information
 * @param {Object} sailData - sail polar information
 * @param {Object} legend - sail polar legend
 */
function generatePolarPlotly (polType, polarName, data) {
   if (!data.array || data.array.length < 2) return;
   let maxMaxVal = Math.ceil (data.max);

   const windSpeeds = data.array[0].slice(1).map(v => parseFloat(v)).filter(v => !isNaN(v));
   const twaValues = data.array.slice(1).map(row => parseFloat(row[0])).filter(v => !isNaN(v));
   let maxTWS = Math.ceil(windSpeeds[windSpeeds.length - 1] * 1.1);
   const initialTWS = (polType === POL_TYPE.WIND_POLAR) ? 15 : 0;
   const what =  (polType === POL_TYPE.WIND_POLAR) ? "TWS" : "Height";
   // polType = POL_TYPE.WIND_POLAR;

   const chartContainer = document.createElement("div");
   chartContainer.innerHTML = `
      <label for="windSpeed">${what}:</label>
      <input type="range" id="windSpeedSlider" min="${windSpeeds[0]}" max="${maxTWS}" step="0.1" value="${initialTWS}">
      <span id="windSpeedValue">${initialTWS}</span>
      <div id="polarPlotly" style="width: 100%; height: 400px;"></div>
      <p><strong>Max:</strong> <span id="maxSpeed">-</span></p>
   `;
    if (polType === POL_TYPE.WIND_POLAR) {
       chartContainer.innerHTML += `
          <p><strong>Front VMG:</strong> <span id="bestVmg">-</span> kn at <span id="bestVmgAngle">-</span>°</p>
          <p><strong>Back VMG:</strong> <span id="bestVmgBack">-</span> kn at <span id="bestVmgBackAngle">-</span>°</p>
       `;
    }
   let moreButton = data.fromJson && data.arraySail && Array.isArray(data.arraySail) && data.arraySail.length > 0;
   Swal.fire({
      title: `${polarName} Max: ${data.max}`,
      html: chartContainer,
      width: '50%', // valeur fallback
      customClass: { popup: 'polar-popup' },
      showConfirmButton: true,
      showDenyButton: moreButton, // button appear only if different sails
      showCancelButton: true,
      confirmButtonText: 'Dump Table',
      denyButtonText: 'More',
      cancelButtonText: 'Cancel',     
      denyButtonColor: '#6b7280',
   }).then((result) => {
      if (result.isConfirmed) {
         showPolarTable(polType, polarName, data);
      }
      else if (moreButton && result.isDenied) {
         moreInfoAboutPol (polType, polarName, data);
      }
   });

   function updatePlot (polType, tws) {
      document.getElementById ("windSpeedValue").innerText = `${tws.toFixed(1)}`;

      let speeds = interpolateSpeeds(tws, windSpeeds, data);
      let { fullTwa, fullSpeeds } = symmetrizeData(twaValues, speeds);

      let maxSpeed = findMaxSpeed(fullSpeeds);
      let vmg = bestVmg(fullTwa, fullSpeeds);
      let vmgBack = bestVmgBack(fullTwa, fullSpeeds);

      document.getElementById("maxSpeed").innerText = maxSpeed.toFixed(2);
      if (polType === POL_TYPE.WIND_POLAR) { 
         document.getElementById("bestVmg").innerText = vmg.speed.toFixed(2);
         document.getElementById("bestVmgAngle").innerText = vmg.angle.toFixed(0);
         document.getElementById("bestVmgBack").innerText = vmgBack.speed.toFixed(2);
         document.getElementById("bestVmgBackAngle").innerText = vmgBack.angle.toFixed(0);
      }
      const trace = {
         type: "scatterpolar",
         mode: "lines", // "lines+markers",
         r: fullSpeeds,
         theta: fullTwa,
         name: `Value: ${tws.toFixed(1)}`,
         line: { color: "black" },
         //line: { color: lineColor },
         hovertemplate: `TWA: %{theta}<br>Speed: %{r:.2f} Kn<extra></extra>`
      };

      const layout = {
         polar: {
            radialaxis: { visible: true, range: [0, maxMaxVal || 1] },
            angularaxis: { direction: "clockwise", rotation: 90 }
         },
         showlegend: false
      };

      Plotly.newPlot ("polarPlotly", [trace], layout);
   }

   document.getElementById ("windSpeedSlider").addEventListener("input", function() {
      updatePlot(polType, parseFloat(this.value));
   });

   updatePlot (polType, initialTWS);
}

/**
 * choose a polar then launch polarInfo 
 * @param {string} directory
 * @param {string} current polar name
 */
function choosePolar (dir, currentPolar) {
   const formData = `type=${REQ.DIR}&dir=${dir}&sortByName=true`;
   console.log ("Requête envoyée:", formData);
   const polType = (dir === "pol") ? POL_TYPE.WIND_POLAR : POL_TYPE.WAVE_POLAR;

   fetch(apiUrl, {
      method: "POST",
      headers: {
         "Content-Type": "application/x-www-form-urlencoded"
      },
      body: formData
   })
   .then(response => response.json())
   .then(data => {
      if (!data) {
         Swal.fire("Erreur", "No polar file found", "error");
         return;
      }
     
      console.log("polar: " + JSON.stringify(data));

      // Création du menu déroulant SANS la taille et la date
      const fileOptions = data.map(file => {
         const selected = file[0] === currentPolar ? "selected" : "";
         return `<option value="${file[0]}" ${selected}>${file[0]}</option>`;
      }).join("");

      // Affichage de la boîte de dialogue
      Swal.fire({
         title: "Polar Select",
         html: `
            <div class="swal-wide">
               <select id="polarSelect" class="swal2-select">
                  ${fileOptions}
               </select>
            </div>
         `,
         showCancelButton: true,
         confirmButtonText: "Confirm",
         cancelButtonText: "Cancel",
         customClass: { popup: "swal-wide" },
         preConfirm: () => {
            const selectedFile = document.getElementById("polarSelect").value;
            if (!selectedFile) {
               Swal.showValidationMessage ("Select File");
            }
            return selectedFile;
         }
      }).then(result => {
         if (result.isConfirmed) {
            if (polType === POL_TYPE.WIND_POLAR) {
               polarName = result.value; // Mise à jour du fichier sélectionné
               saveAppState ();          // save in local context
               polarInfo (polType, polarName);
            }
            else {
               polWaveName = result.value;
               polarInfo (polType, polWaveName);
            }
            updateStatusBar ();
         }
      });
   })
   .catch(error => {
      console.error ("Error in file polar request:", error);
      Swal.fire ("Erreur", "Impossible to get File List", "error");
   });
}

