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
            const sailName = sailNames[sailValue].toUpperCase ();
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
         const { bg, luminance } = sailLegend[name.toUpperCase()] ?? sailLegend.NA;
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
   let moreButton = data.report && data.report.length > 0;
   
   // Affichage avec SweetAlert
   Swal.fire({
      title: (polType === POL_TYPE.WIND_POLAR) ? `Speed in Knots Max: ${data.max}` : `Height in meters Max: ${data.max}`,
      html: tableHTML + legendHTML,
      width: "100%",
      showDenyButton: moreButton, // button appear only if report exist
      denyButtonText: 'Report',
      confirmButtonText: 'Back',
      reverseButtons: true,
      footer: `polarName: ${polarName}, nCol: ${data.nCol}, 
         nLine: ${data.nLine}, max: ${data.max}, nSail: ${data.nSail}, fromJson: ${data.fromJson}`,
   }).then((result) => {
      if (result.isConfirmed) {
         generatePolarPlotly (polType, polarName, data);
      }
      else if (moreButton && result.isDenied) {
         let content = data.report.replaceAll (";", "<br>");
         Swal.fire ({
            title: "Report", 
            html: `<div style="text-align:left; padding-left: 10px">${content}</div>`, 
            icon: "warning",
            width: "60%"
         }).then (() => showPolarTable (polType, polarName, data));
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
function oldFindMaxSpeed(speeds) {
   return Math.max(...speeds);
}

/** calculate max and twa at max values */ 
function findMaxSpeed(twaValues, speeds) {
   let bestSpeed = -1, bestAngle = -1;
   for (let i = 0; i < twaValues.length; i++) {
      if (speeds [i] > bestSpeed) {
         bestSpeed = speeds [i];
         bestAngle = twaValues [i];
      }
   }
   if (bestAngle > 180) bestAngle = 360 - bestAngle;
   return { angle: bestAngle, speed: bestSpeed };
}

/** calculate best VMG (Velocity Made Good) when wind is front */ 
function bestVmg(twaValues, speeds) {
   let bestSpeed = -1, bestAngle = -1;
   for (let i = 0; i < twaValues.length; i++) {
      if (twaValues[i] > 90) break;
      let vmg = speeds[i] * Math.cos(twaValues[i] * Math.PI / 180);
      if (vmg > bestSpeed) {
         bestSpeed = vmg;
         bestAngle = twaValues[i];
      }
   }
   if (bestAngle > 180) bestAngle = 360 - bestAngle;
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
   if (bestAngle > 180) bestAngle = 360 - bestAngle;
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
     showCloseButton: true,
     confirmButtonText: 'Back',
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

   const maxMaxVal = Math.ceil (data.max);
   const windSpeeds = data.array[0].slice(1).map(v => parseFloat(v)).filter(v => !isNaN(v));
   const twaValues = data.array.slice(1).map(row => parseFloat(row[0])).filter(v => !isNaN(v));
   const maxTWS = Math.ceil(windSpeeds[windSpeeds.length - 1] * 1.1);
   const initialTWS = (polType === POL_TYPE.WIND_POLAR) ? 15 : 0;
   const what =  (polType === POL_TYPE.WIND_POLAR) ? "TWS" : "Height";
   // polType = POL_TYPE.WIND_POLAR;

   const chartContainer = document.createElement("div");
   chartContainer.innerHTML = `
      <div style="gap: 100px;">
         <label style="font-size: 18px;" for="windSpeed">${what}</label>
         <input type="range" style="width:75%; margin:10px; padding: 10px;"
            id="windSpeedSlider" min="${windSpeeds[0]}" max="${maxTWS}" step="0.1" value="${initialTWS}"
         > 
         <span id="windSpeedValue" style= "font-size: 18px;">${initialTWS.toFixed(2)}</span>
      </div>
      <div id="polarPlotly" style="width: 100%; height: 400px;"></div>
      <p><strong>Max:</strong> <span id="maxSpeed">-</span> ${polType == POL_TYPE.WIND_POLAR ? "kn": "m"} at <span id="maxSpeedAngle">-</span>°</p>
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
      // denyButtonColor: '#6b7280',
   }).then((result) => {
      if (result.isConfirmed) {
         showPolarTable(polType, polarName, data);
      }
      else if (moreButton && result.isDenied) {
         moreInfoAboutPol (polType, polarName, data);
      }
   });

   function fVMG(twa, speed) {
      if (twa <= 90)
         return speed * Math.cos(twa * Math.PI / 180);
      else
         return Math.abs(speed * Math.cos(twa * Math.PI / 180));
   }

   function nearestIndex(value, candidatesArray) {
      let bestIndex = 0;
      let bestDelta = Math.abs(value - candidatesArray[0]);

      for (let i = 1; i < candidatesArray.length; i++) {
         const d = Math.abs(value - candidatesArray[i]);
         if (d < bestDelta) {
            bestDelta = d;
            bestIndex = i;
         }
      }
      return bestIndex;
   }

   function fSail(twa, tws) {
      if (twa > 180) twa = 360 - twa;
      if (!data.arraySail || !Array.isArray(data.arraySail) || data.arraySail.length === 0 || !data.legend) return "NA";
      const sailTable = data.arraySail;
      const sailNames = data.legend;

      // Ligne 0 = TWS (sauf [0][0])
      const twsHeader = sailTable[0];          // [-1, 0, 6, 8, 10, ...]
      // Colonne 0 =TWA
      const twaHeader = sailTable.map(row => row[0]); // [-1, 0, 10, 20, 30, ...]

      const twsIndex = nearestIndex(tws, twsHeader);
      const twaIndex = nearestIndex(twa, twaHeader);
      const sailValue = sailTable[twaIndex][twsIndex] || 0;
      const sailName = sailNames[sailValue] || sailNames[0] || "NA";
      return sailName.toUpperCase();
   }

   function updatePlot (polType, tws) {
      document.getElementById ("windSpeedValue").innerText = `${tws.toFixed(2)} ${polType == POL_TYPE.WIND_POLAR ? "kn": "m"}`;

      let speeds = interpolateSpeeds(tws, windSpeeds, data);
      let { fullTwa, fullSpeeds } = symmetrizeData(twaValues, speeds);

      const customdata = fullTwa.map((twaVal, i) => {
         const thisSpeed = fullSpeeds[i];
         return [
            fVMG(twaVal, thisSpeed),      // customdata[0] = vmg
            fSail(twaVal, tws),           // customdata[1] = sail
            twaVal < 180 ? twaVal : 360 - twaVal
         ];
      });

      let max = findMaxSpeed(fullTwa, fullSpeeds);
      let vmg = bestVmg(fullTwa, fullSpeeds);
      let vmgBack = bestVmgBack(fullTwa, fullSpeeds);

      document.getElementById("maxSpeed").innerText = max.speed.toFixed(2);
      document.getElementById("maxSpeedAngle").innerText = max.angle.toFixed(0);
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
         customdata: customdata,
         hovertemplate:
            'TWA: %{customdata[2]}°<br>' +
            'Speed: %{r:.2f} kn<br>' +
            'VMG: %{customdata[0]:.2f} kn<br>' +
            'Sail: %{customdata[1]}<extra></extra>'
      };

      const layout = {
         polar: {
            bgcolor: "white",
            radialaxis: { visible: true, range: [0, maxMaxVal || 1] },
            angularaxis: { direction: "clockwise", rotation: 90 }
         },
         plot_bgcolor: "green",
         paper_bgcolor: "white",
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
async function choosePolar(dir, currentPolar) {
  const polType = (dir === "pol") ? POL_TYPE.WIND_POLAR : POL_TYPE.WAVE_POLAR;

  const fileName = await chooseFile(dir, currentPolar, true, 'Polar Select'); // wait selection
  if (!fileName) { console.log("No file."); return; }

  if (polType === POL_TYPE.WIND_POLAR) {
    polarName = fileName;        // update global variable
    saveAppState();              // save in local context
    polarInfo(polType, polarName); 
  } else {
    polWaveName = fileName;
    polarInfo(polType, polWaveName);
  }

  updateStatusBar();
}

