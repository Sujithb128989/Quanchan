import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'

export default defineConfig({
  plugins: [react()],
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
