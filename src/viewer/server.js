#!/usr/bin/env node
import http from 'http'
import fs from 'fs'
import path from 'path'
import { fileURLToPath } from 'url'

const __dirname = path.dirname(fileURLToPath(import.meta.url))
const distDir = path.join(__dirname, 'dist')

const args = process.argv.slice(2)
const reportPath = args.find(a => !a.startsWith('-'))

// -p PORT or --port=PORT; default 0 = OS picks a free port
let port = 0
const shortPortIdx = args.indexOf('-p')
if (shortPortIdx !== -1 && args[shortPortIdx + 1]) {
  port = parseInt(args[shortPortIdx + 1])
} else {
  const longPort = args.find(a => a.startsWith('--port='))
  if (longPort) port = parseInt(longPort.split('=')[1])
}

if (!reportPath) {
  console.error('Usage: node server.js <path-to-report.jsonl> [-p PORT]')
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

server.on('error', (err) => {
  if (err.code === 'EADDRINUSE') {
    console.error(`Port ${port} is already in use. Try without -p to use a random port.`)
  } else {
    console.error(err.message)
  }
  process.exit(1)
})

server.listen(port, () => {
  const { port: actualPort } = server.address()
  console.log(`QuarkBot Report Viewer → http://localhost:${actualPort}`)
  console.log(`Report: ${path.resolve(reportPath)}`)
})
