/* jshint esversion: 6 */

/**
 * Displays a SweetAlert2 modal for routing configuration.
 * Loads current values from routeParam and allows reset to defaults.
 */
function launchRouting() {
   const boatNames = competitors.map(c => c.name);

   const getNowISOString = (date = new Date()) => {
      return new Date(date.getTime() - date.getTimezoneOffset() * 60000).toISOString().slice(0, 16);
   };

   const defaultValues = {
      startTimeStr: getNowISOString(),
      isoStep: 1800,
      iBoat: boatNames.length <= 1 ? 1 : 0,
      forbid: true,
      isoc: false,
      withWaves: false,
      withCurrent: false,
      timeInterval: 0,
      nTry: 0
   };

   const current = {
      startTimeStr: routeParam.startTime
         ? getNowISOString(new Date(routeParam.startTime))  // (après correction de la fonction)
         : getNowISOString(), // <-- au premier lancement seulement
      isoStep: routeParam.isoStep ?? defaultValues.isoStep,
      iBoat: routeParam.iBoat ?? defaultValues.iBoat,
      forbid: routeParam.forbid ?? defaultValues.forbid,
      isoc: routeParam.isoc ?? defaultValues.isoc,
      withWaves: routeParam.withWaves ?? defaultValues.withWaves,
      withCurrent: routeParam.withCurrent ?? defaultValues.withCurrent,
      timeInterval: routeParam.timeInterval ?? defaultValues.timeInterval,
      nTry: routeParam.nTry ?? defaultValues.nTry
   };

   const boatOptions = (boatNames.length <= 1 ? "" : `<option value="0"${current.iBoat === 0 ? " selected" : ""}>All</option>`) +
      boatNames.map((name, i) => {
         const val = i + 1;
         const selected = (val === current.iBoat) ? "selected" : "";
         return `<option value="${val}" ${selected}>${name}</option>`;
      }).join("");

   const htmlContent = `
   <div class="swal-grid">
      <label for="swal-startTime">Date & Time:</label>
      <input type="datetime-local" id="swal-startTime" value="${current.startTimeStr}">

      <label for="swal-isoStep">Time Step:</label>
      <select id="swal-isoStep">
         <option value="900"${current.isoStep === 900 ? " selected" : ""}>15 minutes</option>
         <option value="1800"${current.isoStep === 1800 ? " selected" : ""}>30 minutes</option>
         <option value="3600"${current.isoStep === 3600 ? " selected" : ""}>1 hour</option>
         <option value="10800"${current.isoStep === 10800 ? " selected" : ""}>3 hours</option>
      </select>

      <label for="swal-boatSelect">Boat:</label>
      <select id="swal-boatSelect">${boatOptions}</select>

      <label for="swal-model">Model:</label>
      <select id="swal-model">
         <option value="GFS">GFS</option>
         <option value="ECMWF">ECMWF</option>
         <option value="ARPEGE">ARPEGE</option>
         <option value="METEOCONSULT">METEOCONSULT</option>
      </select>
      <label for="swal-forbidZones">Forbid Zones:</label>
      <div><input type="checkbox" id="swal-forbidZones" ${current.forbid ? "checked" : ""}></div>

      <label for="swal-isoc">Isochrones:</label>
      <div><input type="checkbox" id="swal-isoc" ${current.isoc ? "checked" : ""}></div>

      <label for="swal-withWaves">With Waves:</label>
      <div><input type="checkbox" id="swal-withWaves" ${current.withWaves ? "checked" : ""}></div>

      <label for="swal-withCurrent">With Current:</label>
      <div><input type="checkbox" id="swal-withCurrent" ${current.withCurrent ? "checked" : ""}></div>

      <label for="swal-clipBoard">Request in Clipboard:</label>
      <div><input type="checkbox" id="swal-clipBoard" ${clipBoard ? "checked" : ""}></div>

      <h3>Options</h3>

      <label for="swal-timeInterval">Time Interval:</label>
      <select id="swal-timeInterval">
         <option value="0"${current.timeInterval === 0 ? " selected" : ""}>None</option>
         <option value="900"${current.timeInterval === 900 ? " selected" : ""}>15 minutes</option>
         <option value="1800"${current.timeInterval === 1800 ? " selected" : ""}>30 minutes</option>
         <option value="3600"${current.timeInterval === 3600 ? " selected" : ""}>1 hour</option>
         <option value="10800"${current.timeInterval === 10800 ? " selected" : ""}>3 hours</option>
         <option value="21600"${current.timeInterval === 21600 ? " selected" : ""}>6 hours</option>
      </select>

      <label for="swal-nTry">N Try:</label>
      <input type="number" id="swal-nTry" min="0" max="384" value="${current.nTry}">
   </div>

   <div class="swal-footer">
      <button type="button" id="resetBtn" class="swal2-cancel swal2-styled">Reset</button>
      <button type="button" class="swal2-cancel swal2-styled" data-swal2-cancel>Cancel</button>
      <button type="button" class="swal2-confirm swal2-styled" data-swal2-confirm>Launch Route</button>
   </div>`;

   Swal.fire({
      title: 'Route Parameters',
      html: htmlContent,
      customClass: {
         popup: 'swal-wide'
      },
      showConfirmButton: false,
      didOpen: () => {
         const updateNTryState = () => {
            const iBoat = parseInt(document.getElementById("swal-boatSelect").value, 10);
            const timeInterval = parseInt(document.getElementById("swal-timeInterval").value, 10);
            const nTry = document.getElementById("swal-nTry");
            nTry.disabled = (iBoat === 0 && timeInterval !== 0);
         };

         updateNTryState();
         document.getElementById("swal-boatSelect").addEventListener("change", updateNTryState);
         document.getElementById("swal-timeInterval").addEventListener("change", updateNTryState);

         document.getElementById("resetBtn").addEventListener("click", () => {
            document.getElementById("swal-startTime").value = getNowISOString();
            document.getElementById("swal-isoStep").value = "1800";
            document.getElementById("swal-model").value = "GFS";
            document.getElementById("swal-boatSelect").value = (boatNames.length <= 1) ? "1" : "0";
            document.getElementById("swal-forbidZones").checked = true;
            document.getElementById("swal-isoc").checked = false;
            document.getElementById("swal-withWaves").checked = false;
            document.getElementById("swal-withCurrent").checked = false;
            document.getElementById("swal-clipBoard").checked = false;
            document.getElementById("swal-timeInterval").value = "0";
            document.getElementById("swal-nTry").value = "0";
            updateNTryState();
         });

         Swal.getPopup().querySelector('[data-swal2-cancel]').addEventListener("click", () => {
            Swal.close();
         });

         Swal.getPopup().querySelector('[data-swal2-confirm]').addEventListener("click", () => {
            setTimeout(() => {
               const startTimeStr = document.getElementById("swal-startTime").value;
               const isoStep = parseInt(document.getElementById("swal-isoStep").value, 10);
               const iBoat = parseInt(document.getElementById("swal-boatSelect").value, 10);
               const model = document.getElementById("swal-model").value;
               const forbid = document.getElementById("swal-forbidZones").checked;
               const isoc = document.getElementById("swal-isoc").checked;
               const withWaves = document.getElementById("swal-withWaves").checked;
               const withCurrent = document.getElementById("swal-withCurrent").checked;
               clipBoard = document.getElementById("swal-clipBoard").checked; // global variable
               const timeInterval = parseInt(document.getElementById("swal-timeInterval").value, 10);
               const nTry = parseInt(document.getElementById("swal-nTry").value, 10) || 0;

               if ((nTry > 1) && (timeInterval !== 0) && (iBoat === 0)) {
                  Swal.showValidationMessage(
                     'All competitors selection is incompatible with best time option'
                  );
                  return;
               }

               routeParam.startTimeStr = startTimeStr;
               routeParam.startTime = new Date(startTimeStr);
               routeParam.isoStep = isoStep;
               routeParam.iBoat = iBoat;
               routeParam.forbid = forbid;
               routeParam.isoc = isoc;
               routeParam.withWaves = withWaves;
               routeParam.withCurrent = withCurrent;
               routeParam.timeInterval = timeInterval;
               routeParam.nTry = nTry;
               routeParam.model = model;

               updateStatusBar();
               console.log ("Updated Route Parameters:", routeParam);
               Swal.close();
               request();
            }, 0); // <<< important here
         });
      }
   });
}

