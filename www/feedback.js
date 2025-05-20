/**
 * Displays a feedback form using SweetAlert2 and sends the data as plain text.
 * 
 * @param {string} apiUrl - The API endpoint where the feedback will be sent.
 */
function feedback (apiUrl) {
   Swal.fire({
      title: "Submit Feedback",
      html: `
         <table class="swal2-feedback-custom-table">
            <tr>
               <td class="swal2-feedback-label">Email:</td>
               <td><input type="email" id="swal-email" class="swal2-feedback-input" placeholder="Your email" required></td>
            </tr>
            <tr>
               <td class="swal2-feedback-label">Message:</td>
               <td><textarea id="swal-message" class="swal2-feedback-textarea" placeholder="Your message" required></textarea></td>
            </tr>
         </table>
      `,
      showCancelButton: true,
      confirmButtonText: "Send",
      preConfirm: () => {
         const email = document.getElementById ("swal-email").value.trim();
         const message = document.getElementById ("swal-message").value.trim();

         // Simple email validation
         if (!/^\S+@\S+\.\S+$/.test(email)) {
            Swal.showValidationMessage ("Please enter a valid email address!");
            return false;
         }
         if (!message) {
            Swal.showValidationMessage ("Message cannot be empty!");
            return false;
         }

         // Format the data as plain text
         const textData = `type=10&feedback=Email: ${email}\n${message}`;

         // Send data as plain text
         return fetch(apiUrl, {
            method: "POST",
            headers: { "Content-Type": "text/plain" },
            body: textData
         })
         .then(response => {
            if (!response.ok) throw new Error ("Network error");
            return response.text(); // Expecting a plain text response
         })
         .catch(error => {
            Swal.showValidationMessage ("Failed to send feedback: " + error.message);
            return false;
         });
      }
   }).then((result) => {
      if (result.isConfirmed) {
         Swal.fire ("Thank You!", "Your feedback has been sent.", "success");
      }
   });
}

