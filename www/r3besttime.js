/* jshint esversion: 6 */

/**
 * Displays a histogram of the best times based on the given report.
 *
 * @param {Array} - result - The table with duration info.
 * @param {Date} - start - Date of start
 * @param {number} -tInterval - Time in seconds between each tentatives
 */
function dispBestTimeHistogram (result, start, tInterval) {
   if (! result || result.length === 0) {
      Swal.fire({
         icon: 'warning',
         title: 'No data',
         text: 'No data available for best time',
      });
      return;
   }
   let bestTime = Math.min (...result); // Find best duration 
   let worstTime = Math.max (...result); // Find worst duration
   // Conversion des durées en heures
   let timesInHours = result.map (time => time / 3600);
  
   let startEpoch = Math.floor (start.getTime () / 1000);

   // timestamps for X axis
   let timeLabels = result.map((_, index) => new Date((startEpoch + index * tInterval) * 1000));
   let colors = result.map (time => time === bestTime ? 'green' : 'gray');

   let hoverTexts = timeLabels.map((date, i) => {
      return `Start time: ${dateToStr (date)}<br>Duration: ${formatDuration (timesInHours [i] * 3600)}`;
   });

   let trace = {
      x: timeLabels,
      y: timesInHours,
      type: 'bar',
      marker: { color: colors },
      text: hoverTexts,
      hovertemplate: '%{text}<extra></extra>',  // <extra></extra> to delete trace name
      textposition: 'none',
      orientation: 'v'
   };

   let layout = {
      title: 'Route duration depends on time start',
      xaxis: { title: 'Departure time', type: 'date', zeroline: true },
      yaxis: { title: 'Duration (hours)', zeroline: true, zerolinewidth: 2, zerolinecolor: '#000', rangemode: 'tozero' },
      bargap: 0.5,
      hoverlabel: {
         bgcolor: 'black',
         font: { color: 'white' }
      }
   };
   let minDuration = formatDuration (bestTime);
   let maxDuration = formatDuration (worstTime);
   let windowTimeBegin = dateToStr (new Date(startEpoch * 1000));
   let windowTimeEnd = dateToStr (new Date(startEpoch * 1000 + (result.length - 1) * tInterval * 1000));

   Swal.fire({
      title: 'Best time analyze',
      html: `<p><strong>Number of simulation :</strong> ${result.length}</p>
             <p><strong>Window time :</strong> ${windowTimeBegin} - ${windowTimeEnd}</p>
             <p><strong>Min. Duration :</strong> ${minDuration}</p>
             <p><strong>Max. Duration :</strong> ${maxDuration}</p>
             <div id="plotContainer" style="width:100%;height:400px;"></div>`,
      didOpen: () => Plotly.newPlot('plotContainer', [trace], layout),
      width: '80%',
      confirmButtonText: 'Close',
   });
}
