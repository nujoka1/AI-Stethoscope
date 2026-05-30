# Dashboard — Hosting & Testing Guide

This guide shows how to test the local site and publish the Dashboard (`Dashboard/index.html`). Keep it short and actionable.

## Quick local test (static files)
From the repository root or the `Dashboard` folder, run a simple HTTP server and open `index.html` in the browser.

Using Python 3 built-in server:

```bash
cd Dashboard
python3 -m http.server 8000
# open http://localhost:8000 in your browser
```

Using Node (`http-server`):

```bash
npm install -g http-server
cd Dashboard
http-server -p 8000
# open http://localhost:8000
```

VS Code: use the Live Server extension and open the `Dashboard` folder, then click "Go Live".

## Verify frontend behavior
- Open browser DevTools (F12): Console, Network, and Application tabs.
- Check `assets/js/config.js` for API endpoints and update them to point at your local backend if needed.
- In Network tab, confirm all assets load (status 200). Fix 404s by correcting relative paths.
- Use Console to view runtime errors and fix missing variables or CORS failures.

## Testing with a backend locally
If the Dashboard calls an API (e.g., a local server), run the backend and ensure the dashboard `config.js` points to that server (http://localhost:PORT).

If APIs are on another port, enable CORS on the backend or run a reverse proxy (nginx) to serve both frontend and API under the same origin.

To temporarily expose your local backend for remote testing use `ngrok`:

```bash
ngrok http 5000   # if backend runs on port 5000
# update frontend endpoints to the ngrok URL
```

## Deploy options (pick one)

1) GitHub Pages (easy, free for static sites)
- Option A — `docs/` folder on `main`:
  - Move `Dashboard` contents into `docs/` and push.
  - In GitHub repo Settings → Pages select `main` branch `/docs` folder.
  - Site will be available at `https://<your-user>.github.io/<repo>`.
- Option B — `gh-pages` branch using `gh-pages` npm tool:
  - `npm install --save-dev gh-pages`
  - Add deploy script and publish; see gh-pages docs.

2) Netlify (zero-config, supports redirects)
- Connect your GitHub repo in Netlify dashboard, set publish directory to `Dashboard`, and deploy.
- Or drag-and-drop the `Dashboard` folder in Netlify Drop for a quick publish.

3) Vercel (easy Git-based deploys)
- Connect repo and set root to `Dashboard` (or setup project settings) and deploy.

4) Serve from a web server (nginx/Apache)
- Copy `Dashboard` folder to the server's document root.
- Configure TLS with LetsEncrypt for production.

## Post-deploy checks
- Open the site URL and check DevTools for console errors and failing network requests.
- Verify API endpoints work and CORS/HTTPS are correct.
- Test on mobile and desktop (responsive checks).

## Continuous deployment tips
- Use CI (GitHub Actions) to build and automatically deploy to GitHub Pages/Netlify/Vercel on push to `main`.

## Troubleshooting
- 404 on assets: adjust relative paths or base href.
- Mixed content (HTTP assets on HTTPS site): ensure API/asset URLs use HTTPS in production.
- CORS errors: enable CORS on backend or proxy through the same origin.

---
If you want, I can add a small `serve.sh` to run the local server and a `deploy-to-github-pages.sh` script to automate publishing. Which deploy target do you prefer (GitHub Pages / Netlify / Vercel)?