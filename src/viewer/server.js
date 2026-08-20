#!/usr/bin/env node
import http from 'http'
import fs from 'fs'
import path from 'path'
import { fileURLToPath } from 'url'

const __dirname = path.dirname(fileURLToPath(import.meta.url))
const distDir = path.join(__dirname, 'dist')

const args = process.argv.slice(2)
const reportPath = args.find(a => !a.startsWith('--'))
const portArg = args.find(a => a.startsWith('--port='))
const port = portArg ? parseInt(portArg.split('=')[1]) : 3000

if (!reportPath) {
  console.error('Usage: node server.js <path-to-report.jsonl> [--port=3000]')
  process.exit(1)
}

if (!fs.existsSync(reportPath)) {
  console.error(`File not found: ${reportPath}`)
  process.exit(1)
}

const MIME = {
  '.html': 'text/html',
  '.js': 'application/javascript',
  '.css': 'text/css',
  '.svg': 'image/svg+xml',
  '.ico': 'image/x-icon',
  '.woff2': 'font/woff2',
}

const server = http.createServer((req, res) => {
  if (req.url === '/api/report') {
    const stat = fs.statSync(reportPath)
    res.writeHead(200, {
      'Content-Type': 'text/plain; charset=utf-8',
      'Content-Length': stat.size,
      'Cache-Control': 'no-cache',
    })
    fs.createReadStream(reportPath).pipe(res)
    return
  }

  const urlPath = req.url?.split('?')[0] ?? '/'
  const filePath = path.join(distDir, urlPath === '/' ? 'index.html' : urlPath)
  const ext = path.extname(filePath)

  if (fs.existsSync(filePath) && fs.statSync(filePath).isFile()) {
    res.writeHead(200, { 'Content-Type': MIME[ext] ?? 'application/octet-stream' })
    fs.createReadStream(filePath).pipe(res)
  } else {
    // SPA fallback — serve index.html for client-side routing
    const indexPath = path.join(distDir, 'index.html')
    if (fs.existsSync(indexPath)) {
      res.writeHead(200, { 'Content-Type': 'text/html' })
      fs.createReadStream(indexPath).pipe(res)
    } else {
      res.writeHead(404)
      res.end('Build not found. Run: npm run build')
    }
  }
})

server.listen(port, () => {
  console.log(`QuarkBot Report Viewer → http://localhost:${port}`)
  console.log(`Report: ${path.resolve(reportPath)}`)
})
