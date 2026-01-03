import js from "@eslint/js";
import globals from "globals";
import { defineConfig } from "eslint/config";

export default defineConfig([
  js.configs.recommended, // ← au lieu de "extends: ['js/recommended']"
  {
    languageOptions: { globals: globals.browser },
    rules: {
      "no-undef": "off",
      "no-unused-vars": "off"
    }
  }
]);

