export default [
   {
      files: ["**/*.js"],
      languageOptions: {
         ecmaVersion: 2022,
         sourceType: "module",
      },
      rules: {
         "no-var": "warn",         // déconseille var, préfère let/const
         "eqeqeq": "warn",          // déconseille ==, préfère ===
         //"no-unused-vars": "warn",  // avertit si des variables ne sont pas utilisées
         "semi": ["warn", "always"], // préfère toujours mettre un ;
      },
   },
];

