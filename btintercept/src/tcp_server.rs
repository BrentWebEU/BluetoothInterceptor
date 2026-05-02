use std::io::Write;
use std::net::{TcpListener, TcpStream};

use crate::{debug_print, error_print, info_print};

/// Create a TCP server socket bound to the given port.
pub fn create_server(port: u16) -> Result<TcpListener, String> {
    let listener = TcpListener::bind(format!("0.0.0.0:{}", port)).map_err(|e| {
        error_print!("Failed to bind TCP socket on port {}: {}", port, e);
        e.to_string()
    })?;
    info_print!("TCP server listening on port {}", port);
    Ok(listener)
}

/// Accept one client from a non-blocking listener. Returns None if no client is pending.
pub fn accept_client(listener: &TcpListener) -> Option<TcpStream> {
    match listener.accept() {
        Ok((stream, addr)) => {
            info_print!("TCP client connected: {}", addr);
            Some(stream)
        }
        Err(ref e) if e.kind() == std::io::ErrorKind::WouldBlock => None,
        Err(e) => {
            error_print!("Failed to accept TCP connection: {}", e);
            None
        }
    }
}

/// Send a byte slice to a connected TCP client.
pub fn send_data(stream: &mut TcpStream, data: &[u8]) -> Result<usize, String> {
    stream.write_all(data).map_err(|e| {
        error_print!("Failed to send data to TCP client: {}", e);
        e.to_string()
    })?;
    debug_print!("Sent {} bytes to TCP client", data.len());
    Ok(data.len())
}
