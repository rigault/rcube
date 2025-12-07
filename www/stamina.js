const extractStam = '{"recovery":{"points":1,"loWind":0,"hiWind":30,"loTime":5,"hiTime":15},"tiredness":[20,50],"consumption":{"points":{"tack":10,"gybe":10,"sail":20},"winds":{"0":1,"10":1.2,"20":1.5,"30":2},"boats":{"0":1,"5":1.2,"15":1.5,"50":2}},"impact":{"0":2,"100":0.5}}';

const shipParam = [
   {
      name: "Imoca",
      cShip: 1.2,
      tMin: [300, 300, 420],
      tMax: [660, 660, 600]
   },
   {
      name: "Ultim",
      cShip: 1.5,
      tMin: [300, 300, 420],
      tMax: [660, 660, 600]
   },
   {
      name: "Normal",
      cShip: 1.0,
      tMin: [300, 300, 336],
      tMax: [660, 660, 480]
   }
];

/**
 * Compute the penalty in seconds for a manoeuvre type in Virtual Regatta.
 * Depends on true wind speed (tws) and energy. Also computes the stamina coefficient.
 * 
 * @param {number} shipIndex - Index of the ship in ship parameters.
 * @param {number} type - Type of manoeuvre (0, 1, or 2).
 * @param {number} tws - True wind speed in knots.
 * @param {number} energy - Current energy (0 to 100).
 * @param {object} staminaK - stamina coefficient.
 * @returns {number} Penalty time in seconds, or -1 if the type is invalid.
 */
function fPenalty (shipIndex, type, tws, energy, staminaK, fullPack) {
   if (type < 0 || type > 2) {
      console.error(`In fPenalty, type unknown: ${type}`);
      return -1;
   }
   const cShip = shipParam[shipIndex].cShip;
   const tMin = shipParam[shipIndex].tMin[type];
   const tMax = shipParam[shipIndex].tMax[type];
   const fTws = 50.0 - 50.0 * Math.cos(Math.PI * ((Math.max(10.0, Math.min(tws, 30.0)) - 10.0) / 20.0));
   let t = tMin + fTws * (tMax - tMin) / 100.0;
   if (fullPack) t *= 0.8;
   return t * cShip * staminaK;
}

/**
 * Compute the point loss for a manoeuvre in Virtual Regatta.
 * Depends on true wind speed (tws) and whether the full pack is active.
 * 
 * @param {number} shipIndex - Index of the ship in ship parameters.
 * @param {number} type - Type of manoeuvre (0, 1, or 2).
 * @param {number} tws - True wind speed in knots.
 * @param {boolean} fullPack - Whether the full pack option is active.
 * @returns {number} Point loss.
 */
function fPointLoss (shipIndex, type, tws, fullPack) {
   const fPCoeff = fullPack ? 0.8 : 1.0;
   const loss = (type === 2) ? 0.2 : 0.1;
   const cShip = shipParam[shipIndex].cShip;

   const fTws = (tws < 10.0) ? 0.02 * tws + 1.0 :
                (tws <= 20.0) ? 0.03 * tws + 0.9 :
                (tws <= 30.0) ? 0.05 * tws + 0.5 :
                2.0;

  return fPCoeff * loss * cShip * fTws;
}

/**
 * Compute the time in seconds needed to recover one energy point, depending on true wind speed.
 * 
 * @param {number} tws - True wind speed in knots.
 * @returns {number} Time in seconds to recover one energy point.
 */
function fTimeToRecupOnePoint (tws) {
   const timeToRecupLow = 5.0 * 60.0;   // 5 minutes in seconds
   const timeToRecupHigh = 15.0 * 60.0; // 15 minutes in seconds
   const fTws = 1.0 - Math.cos(Math.PI * (Math.min(tws, 30.0) / 30.0));

   return timeToRecupLow + fTws * (timeToRecupHigh - timeToRecupLow) / 2.0;
}

/**
 * Opens a SweetAlert2 modal acting as a live stamina & penalty calculator for Virtual Regatta.
 *
 * This function creates a well-aligned UI with:
 * - A dropdown for boat type.
 * - Sliders for TWS and Energy with value display.
 * - A Full Pack checkbox.
 * - Calculated recovery time.
 * - A table showing Time To Manoeuvre and Energy Point Lossed for Tack, Gybe, and Sail.
 *
 * Updates are live on any user interaction.
 */
function stamina() {
   if (! window.matchMedia('(orientation: landscape)').matches) {
      Swal.fire('Stamina Warning', 'Switch to Lay out', 'warning');
      return;
   }

   const shipOptions = shipParam.map((ship, index) => `<option value="${index}">${ship.name}</option>`).join("");

   const htmlContent = `
   <table class="no-lines">
   <tr>
      <td><label for="vrShipSelect" style="min-width: 100px;">Boat</label></td>
      <td><select id="vrShipSelect">${shipOptions}</select></td>
   </tr>

   <tr>
      <td><label for="vrTwsRange" style="min-width: 100px;">TWS</label></td>
      <td><input id="vrTwsRange" type="range" min="0" max="30" value="15"
            style="flex: 1 0 150px; margin: 0 10px; max-width: 150px;"></td>
      <td><span><span id="vrTwsValue">15</span> kts</span></td>
   </tr>

   <tr>
      <td><label for="vrEnergyRange" style="min-width: 100px;">Energy</label></td>
      <td><input id="vrEnergyRange" type="range" min="0" max="100" value="100"
             style="flex: 1 0 150px; margin: 0 10px; max-width: 150px;"></td>
      <td><span><span id="vrEnergyValue" style="min-width: 200px;">100 </span></span></td>
   </tr>

   <tr>
      <td><label for="vrFullPack" style="min-width: 100px;">Full Pack</label></td>
      <td><input id="vrFullPack" type="checkbox" style="margin-right: 5px;"></td>
   </tr>
   <tr>
      <td>Time to recover 1 point:</td> 
      <td><span id="vrRecoverTime"></span></td>
   </tr>
   </table>

   <table style="margin-top: 10px; width: 100%; border-collapse: collapse;">
      <tr>
        <th style="text-align: left;"></th>
        <th>Tack</th>
        <th>Gybe</th>
        <th>Sail</th>
      </tr>
      <tr>
        <td style="text-align: left;">Time To Manoeuvre</td>
        <td id="vrTimeTack"></td>
        <td id="vrTimeGybe"></td>
        <td id="vrTimeSail"></td>
      </tr>
      <tr>
        <td style="text-align: left;">Energy Point Lossed</td>
        <td id="vrLossTack"></td>
        <td id="vrLossGybe"></td>
        <td id="vrLossSail"></td>
      </tr>
   </table>
   `;

   Swal.fire({
      title: "Stamina & Penalty Calculator",
      html: htmlContent,
      customClass: "stamina-popup",  // see css associated
      showCancelButton: true,
      showCloseButton: true,
      focusConfirm: false,
      confirmButtonText: "More",
      didOpen: (popup) => {
         const shipSelect   = popup.querySelector("#vrShipSelect");
         const twsRange     = popup.querySelector("#vrTwsRange");
         const energyRange  = popup.querySelector("#vrEnergyRange");
         const fullPackChk  = popup.querySelector("#vrFullPack");
         const twsValue     = popup.querySelector("#vrTwsValue");
         const energyValue  = popup.querySelector("#vrEnergyValue");
         const recoverTime  = popup.querySelector("#vrRecoverTime");
         const timeCells    = ["#vrTimeTack", "#vrTimeGybe", "#vrTimeSail"].map(id => popup.querySelector(id));
         const lossCells    = ["#vrLossTack", "#vrLossGybe", "#vrLossSail"].map(id => popup.querySelector(id));

         function formatTimeSecToMnS(time) {
            const m = Math.floor(time / 60).toString().padStart(2, "0");
            const s = Math.round(time % 60).toString().padStart(2, "0");
            return `${m} mn ${s} s`;
         }

         function update () {
            const kPenalty = 0.015;
            const shipIndex = parseInt(shipSelect.value);
            const tws = parseFloat(twsRange.value);
            const energy = parseFloat(energyRange.value);
            const fullPack = fullPackChk.checked;
            const staminaOut = 2 - Math.min (energy, 100.0) * kPenalty;

            twsValue.textContent = tws.toFixed(0);
            energyValue.textContent = `${energy.toFixed(2).padStart (3, "0")}% (${staminaOut.toFixed(2)})`;

            const recup = fTimeToRecupOnePoint(tws);
            recoverTime.textContent =  formatTimeSecToMnS (recup);

            for (let i = 0; i < 3; i++) {
               const time = fPenalty (shipIndex, i, tws, energy, staminaOut, fullPack);
               const loss = fPointLoss (shipIndex, i, tws, fullPack);
               timeCells[i].textContent =  formatTimeSecToMnS(time);
               lossCells[i].textContent = (100 * loss).toFixed(2) + " %";
            }
         }

         shipSelect.addEventListener ("change", update);
         twsRange.addEventListener ("input", update);
         energyRange.addEventListener ("input", update);
         fullPackChk.addEventListener ("change", update);

         update ();
      }
   }).then ((result)=>{
      const content = JSON.stringify(JSON.parse(extractStam), null, 2);
      if (result.isConfirmed) {
         Swal.fire ({
            title: 'extractStam',
            html: `<pre style="text-align:left;white-space:pre-wrap;margin:0">${content}</pre>`
         });
      }
   });
}
