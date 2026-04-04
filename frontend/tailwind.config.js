/** @type {import('tailwindcss').Config} */
export default {
  content: ['./index.html', './src/**/*.{js,ts,jsx,tsx}'],
  theme: {
    extend: {
      colors: {
        phoenix: {
          red: '#ff4500',
          orange: '#ff6a00',
          yellow: '#ffa500',
        },
        quantum: {
          cyan: '#00f0ff',
          blue: '#0080ff',
          purple: '#8000ff',
          white: '#e0f8ff',
        },
        dark: {
          900: '#000000',
          800: '#050508',
          700: '#0a0a12',
          600: '#0f0f1a',
          500: '#141420',
        },
      },
      fontFamily: {
        mono: ['JetBrains Mono', 'Fira Code', 'monospace'],
        sans: ['Inter', 'system-ui', 'sans-serif'],
      },
      backgroundImage: {
        'neon-glow': 'radial-gradient(ellipse at center, rgba(255,69,0,0.15) 0%, transparent 70%)',
        'quantum-glow': 'radial-gradient(ellipse at center, rgba(0,240,255,0.15) 0%, transparent 70%)',
      },
      animation: {
        'pulse-slow': 'pulse 3s cubic-bezier(0.4, 0, 0.6, 1) infinite',
        'spin-slow': 'spin 8s linear infinite',
        'float': 'float 6s ease-in-out infinite',
        'scan': 'scan 8s linear infinite',
        'glow-pulse': 'glowPulse 2s ease-in-out infinite',
        'fade-in': 'fadeIn 0.5s ease-in-out forwards',
        'slide-up': 'slideUp 0.4s ease-out forwards',
      },
      keyframes: {
        float: {
          '0%, 100%': { transform: 'translateY(0px)' },
          '50%': { transform: 'translateY(-20px)' },
        },
        scan: {
          '0%': { transform: 'translateY(-100%)' },
          '100%': { transform: 'translateY(100vh)' },
        },
        glowPulse: {
          '0%, 100%': { boxShadow: '0 0 10px rgba(255,69,0,0.5), 0 0 30px rgba(255,69,0,0.2)' },
          '50%': { boxShadow: '0 0 20px rgba(255,69,0,0.9), 0 0 60px rgba(255,69,0,0.4), 0 0 100px rgba(0,240,255,0.2)' },
        },
        fadeIn: {
          '0%': { opacity: '0', transform: 'translateY(10px)' },
          '100%': { opacity: '1', transform: 'translateY(0)' },
        },
        slideUp: {
          '0%': { opacity: '0', transform: 'translateY(20px)' },
          '100%': { opacity: '1', transform: 'translateY(0)' },
        },
      },
      backdropBlur: {
        xs: '2px',
      },
    },
  },
  plugins: [],
};
