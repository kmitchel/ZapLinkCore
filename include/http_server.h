/**
 * @file http_server.h
 * @brief Multi-threaded HTTP server for API endpoints
 * 
 * Provides HTTP endpoints for:
 * - /stream/{channel} - Raw MPEG-TS passthrough
 * - /playlist.m3u     - M3U playlist of available channels
 * - /xmltv.xml        - XMLTV format program guide
 * - /xmltv.json       - JSON format program guide
 * 
 * Each client connection is handled in a separate thread.
 */

#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

/**
 * Start the HTTP server on the specified port
 * This function blocks indefinitely, handling connections
 * @param port TCP port to listen on (default: 18392)
 */
void start_http_server(int port);

#endif
