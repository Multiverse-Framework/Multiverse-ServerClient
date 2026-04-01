use tracing::debug;

pub fn hexdump(data: &[u8], max_show: usize) {
    if data.is_empty() {
        debug!("  <empty buffer>");
        return;
    }

    let show = data.len().min(max_show);
    let mut hex_str = String::with_capacity(show * 3);
    
    for (i, byte) in data.iter().take(show).enumerate() {
        if i > 0 {
            hex_str.push(' ');
        }
        hex_str.push_str(&format!("{:02X}", byte));
    }
    
    if data.len() > show {
        hex_str.push_str(&format!(" ...(+{})", data.len() - show));
    }
    
    debug!("  hex({}): {}", data.len(), hex_str);
}

pub fn ascii_preview(data: &[u8], max_show: usize) -> String {
    if data.is_empty() {
        return "<empty>".to_string();
    }
    
    let show = data.len().min(max_show);
    let mut result = String::with_capacity(show);
    
    for &byte in data.iter().take(show) {
        if (32..=126).contains(&byte) {
            result.push(byte as char);
        } else {
            result.push('.');
        }
    }
    
    if data.len() > show {
        result.push_str("...");
    }
    
    result
}

pub fn dump_payloads(payloads: &[Vec<u8>]) {
    debug!("Payloads: {} part(s)", payloads.len());
    for (i, payload) in payloads.iter().enumerate() {
        debug!("  [{}] {} bytes: {}", i, payload.len(), ascii_preview(payload, 64));
    }
}