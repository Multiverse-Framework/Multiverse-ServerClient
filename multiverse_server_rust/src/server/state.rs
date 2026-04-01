use std::fmt;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ServerState {
    ReceiveRequestMetaData,
    BindObjects,
    SendResponseMetaData,
    ReceiveSendData,
    BindSendData,
    BindReceiveData,
    SendReceiveData,
}

impl fmt::Display for ServerState {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            ServerState::ReceiveRequestMetaData => write!(f, "ReceiveRequestMetaData"),
            ServerState::BindObjects => write!(f, "BindObjects"),
            ServerState::SendResponseMetaData => write!(f, "SendResponseMetaData"),
            ServerState::ReceiveSendData => write!(f, "ReceiveSendData"),
            ServerState::BindSendData => write!(f, "BindSendData"),
            ServerState::BindReceiveData => write!(f, "BindReceiveData"),
            ServerState::SendReceiveData => write!(f, "SendReceiveData"),
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum MetaDataState {
    Normal,
    Reset,
    WaitAfterSendReceiveData,
    WaitAfterOtherBindSendData,
    WaitAfterOtherSendRequestMetaData,
    WaitAfterOtherNormal,
}

impl Default for MetaDataState {
    fn default() -> Self {
        MetaDataState::Normal
    }
}