/* jshint esversion: 6 */

/**
 * condense list of time stamps in concise way 
 * @param {Object} list of timesStamps
 */
const models = ["GFS", "ECMWF", "ARPEGE", "AROME", "METEOCONSULT"];

function condenseTimeStamps (timeStamps) {
   let result = [];
   if (timeStamps.length === 0) return "[]";
   if (timeStamps.length < 5) {
      for (let i = 0; i < timeStamps.length; i++) {
         result.push (timeStamps [i]);
      }
      return "[" + result.join(", ") + "]";
   }

   let start = timeStamps[0];
   let prev = start;
   let timeStep = null;
   let diff = null;
   let afterStart = timeStamps [1];
   for (let i = 1; i < timeStamps.length; i++) {
      diff = timeStamps[i] - prev;

      if (timeStep === null) {
         timeStep = diff; // Initialize first time step
      }

      if (diff !== timeStep) {
         // New sequence found
         if (prev !== start) {
            result.push(start + (prev !== start + timeStep ? ", " + afterStart + ".." + prev : ""));
            afterStart = start + diff;
         } else {
            result.push(start);
         }
         start = timeStamps[i];
         timeStep = diff;
      }

      prev = timeStamps[i];
   }
   afterStart = start + diff;

   // Add last segment
   if (prev !== start) {
      result.push(start + ", " + afterStart + ".." + prev);
   } else {
      result.push(start);
   }

   return "[" + result.join(", ") + "]";
}

/**
 * display meta information about remote grib file 
 * update gribLimits object 
 * @param {string} directory
 * @param {string} current grib name
 */
function gribInfo(dir, model, gribName) {
   const gribParam = model ? `model=${model}` : `grib=${dir}/${gribName}`;
   const formData = `type=${REQ.GRIB}&${gribParam}`;
   console.log ("Request sent:", formData);
   const formatLat = x => (x < 0) ? -x + "°S" : x + "°N";
   const formatLon = x => (x < 0) ? -x + "°W" : x + "°E";
   const headers = { "Content-Type": "application/x-www-form-urlencoded" };
   const token = btoa(`${userId}:${password}`);
   const auth = `Basic ${token}`;
   // console.log ("token: " + token);
   if (auth && userId !== "") headers.Authorization = auth;     // else stay anonymous level 0

   fetch(apiUrl, {
      method: "POST",
      headers,
      body: formData,
      cache: "no-store"
   })
   .then(response => {
      if (!response.ok) {
         throw new Error(`Error ${response.status}: ${response.statusText}`);
      }
      return response.json();
   })
   .then(data => {
      console.log ("JSON received:", JSON.stringify(data));
      if (data._Error) {
         Swal.fire({ icon: 'error', title: 'Error', text: `Error: ${data._Error}` });
         return;
      }
      if (Object.keys(data).length === 0) {
         Swal.fire({ icon: 'error', title: 'Error', text: 'Empty Grib or format error' });
         return;
      }
      if (dir === "grib") {
         if (model) {
            gribLimits.name = model;
            updateStatusBar ();
         }
         gribLimits.bottomLat = data.bottomLat;
         gribLimits.leftLon = data.leftLon;
         gribLimits.topLat = data.topLat;
         gribLimits.rightLon = data.rightLon;
         const bounds = [[gribLimits.bottomLat, gribLimits.leftLon],[gribLimits.topLat, gribLimits.rightLon]];
         map.fitBounds(bounds);
      }

      let shortNames = Array.isArray(data.shortNames) ? data.shortNames.join(", ") : "Not present";

      let rows = [
         ["Centre", `${data.centreName} (ID ${data.centreID})`],
         ["Time Run", `${data.runStart.slice(11, 13)}Z`],
         ["Window time UTC", `${data.runStart} - ${data.runEnd}`],
         ["File", `${data.fileName} (${data.fileSize.toLocaleString("fr-FR")} bytes)`],
         ["latStep / lonStep", `${data.latStep}° / ${data.lonStep}°`],
         ["Zone", `${data.nLat} x ${data.nLon} values: lat: ${formatLat (data.topLat)} to ${formatLat (data.bottomLat)}, lon: ${formatLon (data.leftLon)} to ${formatLon (data.rightLon)}`],
         ["Short Names", shortNames],
         ["Time Stamps", `${data.nTimeStamp} values: ${condenseTimeStamps(data.timeStamps)}`]
      ];
     if (data.Info.length >= 2) rows.push (["Info", `${data.Info}`]);

      const content = `
      <div style="padding: 16px; font-family: Arial, sans-serif;">
         <table style="border-collapse: collapse; width: 100%; text-align: left; font-size: 14px;">
            <tbody>
               ${rows.map(([key, value], index) => `
                  <tr style="background-color: ${index % 2 === 0 ? '#f9f9f9' : '#ffffff'}; text-align: left;" >
                     <td style="padding: 8px 12px; font-weight: bold; border-bottom: 1px solid #ddd; text-align: left;" >${key}</td>
                     <td style="padding: 8px 12px; border-bottom: 1px solid #ddd; text-align: left;" >${value}</td>
                  </tr>
               `).join('')}
            </tbody>
         </table>
      </div>`;

      Swal.fire({
         title: "Grib Info",
         html: content,
         icon: "success",
         confirmButtonText: "OK",
         width: "50%",
         confirmButtonColor: "#FFA500"
      });
   })
   .catch(error => {
      console.error("Error:", error);
      Swal.fire({
         title: "Error",
         text: error.message,
         icon: "error",
         confirmButtonText: "OK",
         confirmButtonColor: "#FFA500"
      });
   });
}

async function selectModel() {
   let options = {};
   models.forEach(m => {
      options[m] = m; // key = value
   });

   // Affiche la modale
   const { value: model } = await Swal.fire({
      title: 'Model selection',
      input: 'select',
      inputOptions: options,
      inputPlaceholder: 'Choose Model',
      showCancelButton: true
   });
   if (!model) return;
   gribInfo ("grib", model);
}

/**
 * choose a grib file then launch gribInfo 
 * @param {string} directory
 * @param {string} current grib name
 */
function chooseGrib (dir, fileName) {
   const formData = `type=${REQ.DIR}&dir=${dir}`;
   console.log ("Request sent:", formData);
   const headers = { "Content-Type": "application/x-www-form-urlencoded" };
   const token = btoa(`${userId}:${password}`);
   const auth = `Basic ${token}`;
   console.log ("token: " + token);
   if (auth && userId !== "") headers.Authorization = auth;     // else stay anonymous level 0
   fetch(apiUrl, {
      method: "POST",
      headers,
      body: formData,
      cache: "no-store"
   })
   .then(response => response.json())
   .then(data => {
      if (!Array.isArray(data) || data.length === 0) {
         Swal.fire ("Error", "No Grib File found", "error");
         return;
      }

      // Création du menu déroulant SANS la taille et la date
      const fileOptions = data.map (file => {
         const selected = file[0] === fileName ? "selected" : "";
         return `<option value="${file[0]}" ${selected}>${file[0]}</option>`;
      }).join("");
      // Dialog box display
      Swal.fire({
         title: "Grib Select",
         html: `
            <div class="swal-wide">
               <select id="gribSelect" class="swal2-select">
                  ${fileOptions}
               </select>
            </div>
         `,
         showCancelButton: true,
         confirmButtonText: "Confirm",
         cancelButtonText: "Cancel",
         customClass: { popup: "swal-wide" },
         preConfirm: () => {
         const selectedFile = document.getElementById ("gribSelect").value;
            if (!selectedFile) {
               Swal.showValidationMessage("Select file.");
            }
            return selectedFile;
         }
      }).then(result => {
         if (result.isConfirmed) {
            if (dir === "grib") {
               gribLimits.name = result.value; // update file selected for wind grib only
            }  
            else // current
               gribLimits.currentName = result.value;

            gribInfo (dir, "", result.value);
            updateStatusBar ();
            console.log ("gribLimits: " + gribLimits);
            let currentZoom = map.getZoom();
               map.setZoom (currentZoom - 1);
               setTimeout (() => {
               map.setZoom (currentZoom);
            }, 1000); // Merdique mais ça marche
         }
      });
   })
   .catch(error => {
      console.error("Error requesting grib:", error);
      Swal.fire("Erreur", "Impossible to get file list", "error");
   });
}

