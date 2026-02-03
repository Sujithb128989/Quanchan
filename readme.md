# Quanchan 🍀 (Quantum-Safe Anonymous Imageboard)

Quanchan is an anonymous imageboard designed for the post-quantum era. It leverages a high-performance C++ backend to provide cryptographic guarantees that remain secure even against future quantum computer attacks.

## 🚀 Overview
Traditional imageboards rely on RSA or ECC for security. Quanchan utilizes **Dilithium5** for digital signatures and **Kyber** for key encapsulation, ensuring that user identity (tripcodes) and post integrity are protected.

## 🏗️ System Architecture

### The "Data Size" Solution
A common critique of Dilithium5 is the **4.6 KB signature size**. Quanchan addresses this through:
1. **Signature Aggregation:** Signing thread blocks rather than individual posts to minimize overhead.
2. **Hybrid TLS 1.3:** Combining `X25519` with `Kyber768` for a balance of classical speed and quantum security.
3. **WASM Verification:** Offloading signature verification to the client-side using WebAssembly for a lag-free UI.

### Diagram
```mermaid
graph TD
    subgraph Client_Side [Frontend: Quanchan]
        A[Browser / SPA] -->|Hybrid TLS 1.3 Handshake| B(WASM Liboqs Module)
        B -->|Verify Signature| C{Post Authenticity}
    end

    subgraph Transport_Layer [PQC Security Layer]
        D[Quantum-Safe-Backend] -->|Dilithium5 Batch Signatures| A
        D -->|Kyber768 Key Exchange| A
    end

    subgraph Server_Side [Data & Logic]
        D --> E[(PostgreSQL / MongoDB)]
        D --> F[SHA-3 Tripcode Generator]
        E -->|Stored Posts + Batch Sigs| D
    end

    style B fill:#f96,stroke:#333,stroke-width:2px
    style D fill:#69f,stroke:#333,stroke-width:2px


 ```
## 🛠️ Tech Stack
- **Frontend:** React / Next.js (Project Name: Quanchan)
- **Backend:** C++ [Quantum-Safe-Backend](https://github.com/Sujithb128989/Quantum-Safe-Backend)
- **Cryptography:** [liboqs](https://github.com/open-quantum-safe/liboqs)
- **Database:** PostgreSQL (with SHA-3 hashing)

## 🔒 Security Features
- **Quantum-Safe Tripcodes:** User identities generated via PQC-resistant hash functions.
- **Dilithium5 Authentication:** Every post is cryptographically signed by the server to prevent MITM tampering.
- **Stateless Anonymity:** No tracking cookies; identity is derived strictly from cryptographic keys.

## 🚦 Getting Started
1. Clone the backend: `git clone https://github.com/Sujithb128989/Quantum-Safe-Backend`
2. Install dependencies: `liboqs`, `openssl-3.0` (PQC fork)
3. Build the frontend: `npm install && npm run dev`

---
*Developed as a Capstone Project exploring the practical implementation of NIST PQC standards.*
