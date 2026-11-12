// Screenshots the setup-page previews from portal_preview.py at phone size.
//   node sim/portal_preview.js sim/out/portal
const { chromium } = require('playwright');
const fs = require('fs');
const path = require('path');

(async () => {
  const dir = process.argv[2] || path.join(__dirname, 'out', 'portal');
  const browser = await chromium.launch();
  const page = await browser.newPage({ viewport: { width: 390, height: 844 }, deviceScaleFactor: 2 });
  page.on('pageerror', (e) => console.log('PAGE ERROR', e.message));
  for (const f of fs.readdirSync(dir).filter((f) => f.endsWith('.html')).sort()) {
    await page.goto('file://' + path.resolve(dir, f));
    if (f.includes('wifi')) await page.click('summary');  // show the ADVANCED section too
    await page.waitForTimeout(150);
    const out = path.join(dir, f.replace('.html', '.png'));
    await page.screenshot({ path: out, fullPage: true });
    console.log('shot', out);
  }
  await browser.close();
})();
