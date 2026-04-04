const fs = require('fs');
const png = require('pngjs').PNG;

const file = 'C:\\Users\\Asus\\.gemini\\antigravity\\brain\\7d868ce5-befb-44e2-b852-69796c86e622\\.system_generated\\click_feedback\\click_feedback_1774194253389.png';

fs.createReadStream(file)
  .pipe(new png())
  .on('parsed', function() {
    // Check multiple coordinates
    const coords = [
      {x: 50, y: 500},  // sidebar
      {x: 500, y: 500}, // main content area
      {x: 800, y: 500}  // right sidebar
    ];
    
    for (const {x, y} of coords) {
        if (x < this.width && y < this.height) {
            const idx = (this.width * y + x) << 2;
            const r = this.data[idx];
            const g = this.data[idx + 1];
            const b = this.data[idx + 2];
            console.log(`Pixel at (${x}, ${y}): rgba(${r}, ${g}, ${b}, ${this.data[idx+3]}) -> #${r.toString(16).padStart(2,'0')}${g.toString(16).padStart(2,'0')}${b.toString(16).padStart(2,'0')}`);
        }
    }
  });
