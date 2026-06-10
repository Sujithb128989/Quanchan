/*
 * Copyright (C) 2026 QuanChan
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'

export default defineConfig({
  plugins: [react()],
  build: {
    rollupOptions: {
      output: {
        manualChunks(id) {
          if (id.includes('node_modules')) {
            if (id.includes('node_modules/three/')) {
              return 'vendor-three';
            }
            if (id.includes('node_modules/@noble/post-quantum/')) {
              return 'vendor-crypto';
            }
          }
        }
      }
    }
  },
  server: {
    allowedHosts: true,
    proxy: {
      '/api': {
        target: 'https://localhost:8080',
        secure: false,
        changeOrigin: true,
        configure: (proxy) => {
          proxy.on('proxyRes', (proxyRes) => {
            if (!proxyRes.headers['access-control-expose-headers']) {
              proxyRes.headers['access-control-expose-headers'] = 'X-PQC-Cipher, X-PQC-Proof-Endpoint';
            }
          });
        }
      },
      '/uploads': {
        target: 'https://localhost:8080',
        secure: false,
        changeOrigin: true,
      }
    }
  }
})
