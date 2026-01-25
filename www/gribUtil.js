/**
 * Condense list of time stamps in concise way 
 * @param {number[]} timeStamps
 * @returns {string}
 */
function condenseTimeStamps (timeStamps) {
   let result = [];
   if (!timeStamps || timeStamps.length === 0) return "[]";
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
   if (prev !== start) result.push(start + ", " + afterStart + ".." + prev);
   else result.push(start);

   return "[" + result.join(", ") + "]";
}

