/* jshint esversion: 6 */

/**
 * Opens a SweetAlert2 modal with two tabs to modify route parameters.
 *
 * @param {Object} routeParam - The object containing route parameters.
 */
function changeParam(routeParam) {
   Swal.fire({
      title: 'Edit Route Parameters',
      html: `
         <div class="tab-container">
            <div class="tabs">
               <button class="tab-button active" onclick="switchTab(event, 'behavior')">Behavior</button>
               <button class="tab-button" onclick="switchTab(event, 'technical')">Technical</button>
               <button class="tab-button" onclick="switchTab(event, 'constFlows')">Const Flows</button>
            </div>
            
            <div id="behavior" class="tab-content active">
               <div class="form-group"><label>X Wind:</label><input type="number" id="xWind" min="0.50" max="1.50" step="0.01" value="${routeParam.xWind || ''}"></div>
               <div class="form-group"><label>Max Wind:</label><input type="number" id="maxWind" min="10" max="150" value="${routeParam.maxWind || ''}"></div>
               <div class="form-group"><label>Tack (sec):</label><input type="number" id="penalty0" value="${routeParam.penalty0 || ''}"></div>
               <div class="form-group"><label>Gybe (sec):</label><input type="number" id="penalty1" value="${routeParam.penalty1 || ''}"></div>
               <div class="form-group"><label>Sail Change (sec):</label><input type="number" id="penalty2" value="${routeParam.penalty2 || ''}"></div>
               <div class="form-group"><label>Motor Speed:</label><input type="number" id="motorSpeed" step="0.1" value="${routeParam.motorSpeed || ''}"></div>
               <div class="form-group"><label>Threshold Motor:</label><input type="number" id="threshold" step="0.1" value="${routeParam.threshold || ''}"></div>
               <div class="form-group"><label>Day Efficiency:</label>
                  <input type="number" id="dayEfficiency" min="0.50" max="1.50" step="0.01" value="${routeParam.dayEfficiency || ''}">
               </div>
               <div class="form-group"><label>Night Efficiency:</label>
                  <input type="number" id="nightEfficiency" min="0.50" max="1.50" step="0.01" value="${routeParam.nightEfficiency || ''}">
               </div>
               <div class="form-group"><label>Stamina</label><input type="number" min = "0" max = "110" id="staminaVR" value="${routeParam.staminaVR || ''}"></div>
               <div class="form-group"><label>Initial:</label>
                  <label class="radio-pill" for="amureTribord">
                     <input type="radio" id="amureTribord" name="initialAmure" value="0" ${Number(routeParam?.initialAmure ?? 0) === 0 ? 'checked' : ''}>
                     Tri.
                  </label>
                  <label class="radio-pill" for="amureBabord">
                     <input type="radio" id="amureBabord" name="initialAmure" value="1" ${Number(routeParam?.initialAmure ?? 0) === 1 ? 'checked' : ''}>
                     Bab.
                  </label>
              </div>
            </div>

            <div id="technical" class="tab-content">
               <div class="form-group"><label>COG Step:</label><input type="number" id="cogStep" min="2" max="10" value="${routeParam.cogStep || ''}"></div>
               <div class="form-group"><label>COG Range:</label><input type="number" id="cogRange" min="50" max="180" value="${routeParam.cogRange || ''}"></div>
               <div class="form-group"><label>jFactor:</label><input type="number" id="jFactor" step="0.01" value="${routeParam.jFactor || ''}"></div>
               <div class="form-group"><label>kFactor:</label><input type="number" id="kFactor" min="0" max="1" value="${routeParam.kFactor || ''}"></div>
               <div class="form-group"><label>nSectors:</label><input type="number" id="nSectors" min="90" max="1080" value="${routeParam.nSectors || ''}"></div>
               <div class="form-group">
                  <label for="isoDesc">IsoDesc Focal:</label>
                  <input type="checkbox" style="margin-left: 0" id="isoDesc" ${routeParam.isoDesc ? 'checked' : ''}>
               </div>
            </div>
            <div id="constFlows" class="tab-content">
               <div class="form-group"><label>Const Wind TWS:</label><input type="number" id="constWindTws" min="0" max="100" value="${routeParam.constWindTws || ''}"></div>
               <div class="form-group"><label>Const Wind TWD:</label><input type="number" id="constWindTwd" min="0" max="360" value="${routeParam.constWindTwd || ''}"></div>
               <div class="form-group"><label>Const Wave Height:</label><input type="number" id="constWave" min="0" max="10" value="${routeParam.constWave || ''}"></div>
               <div class="form-group"><label>Const Current Speed:</label><input type="number" id="constCurrentS" min="0" max="10" value="${routeParam.constCurrentS || ''}"></div>
               <div class="form-group"><label>Const Current Dir.</label><input type="number" id="constCurrentD" min="0" max="360" value="${routeParam.constCurrentD || ''}"></div>
            </div>
         </div>
         </div>
      `,
      showCancelButton: true,
      confirmButtonText: 'Save',
      preConfirm: () => {
         return {
            xWind: parseFloat(document.getElementById('xWind').value),
            maxWind: parseInt(document.getElementById('maxWind').value),
            penalty0: parseInt(document.getElementById('penalty0').value),
            penalty1: parseInt(document.getElementById('penalty1').value),
            penalty2: parseInt(document.getElementById('penalty2').value),
            motorSpeed: parseFloat(document.getElementById('motorSpeed').value),
            threshold: parseFloat(document.getElementById('threshold').value),
            dayEfficiency: parseFloat(document.getElementById('dayEfficiency').value),
            nightEfficiency: parseFloat(document.getElementById('nightEfficiency').value),
            staminaVR: parseInt(document.getElementById('staminaVR').value),
            initialAmure: Number(Swal.getPopup().querySelector('input[name="initialAmure"]:checked')?.value ?? 0),
            cogStep: parseInt(document.getElementById('cogStep').value),
            cogRange: parseInt(document.getElementById('cogRange').value),
            jFactor: parseFloat(document.getElementById('jFactor').value),
            kFactor: parseInt(document.getElementById('kFactor').value),
            nSectors: parseInt(document.getElementById('nSectors').value),
            isoDesc: document.getElementById("isoDesc").checked,
            constWindTws: parseFloat(document.getElementById('constWindTws').value),
            constWindTwd: parseInt(document.getElementById('constWindTwd').value),
            constWave: parseFloat(document.getElementById('constWave').value),
            constCurrentS: parseFloat(document.getElementById('constCurrentS').value),
            constCurrentD: parseInt(document.getElementById('constCurrentD').value)
         };
      }
   }).then((result) => {
      if (result.isConfirmed) {
         Object.assign(routeParam, result.value);
         console.log('Updated route parameters:', routeParam);
      }
   });
}

/**
 * Handles tab switching inside the Swal modal.
 * 
 * @param {Event} event - The click event.
 * @param {string} tabId - The ID of the tab to activate.
 */
function switchTab (event, tabId) {
   document.querySelectorAll('.tab-button').forEach(button => button.classList.remove('active'));
   document.querySelectorAll('.tab-content').forEach(content => content.classList.remove('active'));
   event.currentTarget.classList.add('active');
   document.getElementById(tabId).classList.add('active');
}

/*! provide raw json params */
function displayJsonParams () {
   const pretty = JSON.stringify(routeParam, null, 2); // ← indenté

   Swal.fire({
     title: `JSON routeParam`,
     width: '60%',
     confirmButtonText: 'Back',
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
         parDump ();
      }
   })
}

/**
 * display parameter file, after launching fetch request to server 
 *
 */
function parDump() {
   const formData = `type=${REQ.PAR_RAW} `;
   console.log("Request sent:", formData);
   const headers = { "Content-Type": "application/x-www-form-urlencoded" };
   fetch (apiUrl, {
      method: "POST",
      headers,
      body: formData,
      cache: "no-store"
   })
   .then(response => {
      console.log ("Raw response:", response);
      if (!response.ok) {
         throw new Error(`Error ${response.status}: ${response.statusText}`);
      }
      return response.text();
   })
   .then(data => {
      console.log("Server Response:", data);

      let content = `<pre style="text-align: left; font-family: 'Courier New', Courier, monospace; font-size: 14px; background: #f4f4f4; padding: 10px; border-radius: 5px;">${data}</pre>`;

      Swal.fire({
         title: "Parameter Info",
         html: content,
         denyButtonText: "More",
         showDenyButton: true,
         width: "60%",
         confirmButtonColor: "#FFA500"
      }).then ((r) => {if (r.isDenied) displayJsonParams ();})
   })
   .catch(error => {
      console.error("Catched error:", error);
      Swal.fire({
         title: "Error",
         text: error.message,
         icon: "error",
         confirmButtonText: "OK",
         confirmButtonColor: "#FFA500"
      });
   });
}

/**
 * get a subset of parameters on server, after launching fetch request to server
   initialize associated values 
 *
 */
function getParam () {
   const formData = `type=${REQ.PAR_JSON}`;
   console.log ("Request sent:", formData);

   fetch (apiUrl, {
      method: "POST",
      headers: {
         "Content-Type": "application/x-www-form-urlencoded"
      },
      body: formData
   })
   .then (response => response.json())
   .then (data => {
      console.log ("JSON data received:", data);
      gribLimits.name = data.grib;
      gribLimits.currentName = data.currentGrib;
      polarName = data.polar;
      polWaveName = data.wavePolar;
   })
   .catch (error => {
      console.error("Error requesting grib:", error);
      Swal.fire("Erreur", "Impossible de get file list", "error");
   });
}

