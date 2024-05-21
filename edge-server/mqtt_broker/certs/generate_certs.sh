openssl genpkey -algorithm RSA -out ca_key.pem -pkeyopt rsa_keygen_bits:2048
openssl req -x509 -new -nodes -key ca_key.pem -sha256 -days 1024 -out ca_cert.pem -subj "/CN=MyCA"
openssl genpkey -algorithm RSA -out server_key.pem -pkeyopt rsa_keygen_bits:2048
openssl req -new -key server_key.pem -out server_csr.pem -subj "/CN=192.168.86.94"
openssl x509 -req -in server_csr.pem -CA ca_cert.pem -CAkey ca_key.pem -CAcreateserial -out server_cert.pem -days 500 -sha256 -extfile san.cnf -extensions req_ext
openssl genpkey -algorithm RSA -out client_key.pem -pkeyopt rsa_keygen_bits:2048
openssl req -new -key client_key.pem -out client_csr.pem -subj "/CN=client"
openssl x509 -req -in client_csr.pem -CA ca_cert.pem -CAkey ca_key.pem -CAcreateserial -out client_cert.pem -days 500 -sha256
