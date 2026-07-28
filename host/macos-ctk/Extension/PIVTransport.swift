import CryptoTokenKit
import Foundation

final class PIVTransport {
    private let smartCard: TKSmartCard

    init(smartCard: TKSmartCard) {
        self.smartCard = smartCard
    }

    func readCertificate(object: Data) throws -> Data {
        let encodedObject = try transmitChained(PIVCodec.getDataCommand(for: object))
        return try PIVCodec.certificate(from: encodedObject)
    }

    func sign(digest: Data) throws -> Data {
        let response = try transmitChained(try PIVCodec.signCommand(digest: digest))
        return try PIVCodec.dynamicAuthenticationResult(from: response)
    }

    func deriveSharedSecret(peerPublicKey: Data) throws -> Data {
        let response = try transmitChained(
            try PIVCodec.keyAgreementCommand(peerPublicKey: peerPublicKey)
        )
        let secret = try PIVCodec.dynamicAuthenticationResult(from: response)
        guard secret.count == 32 else {
            throw PIVCodecError.malformed("P-256 agreement returned \(secret.count) bytes")
        }
        return secret
    }

    private func transmitChained(_ command: Data) throws -> Data {
        var result = Data()
        var response = try transmit(command)
        result.append(response.payload)
        var rounds = 0
        while response.status >> 8 == 0x61 {
            rounds += 1
            guard rounds <= 8 else {
                throw PIVCodecError.malformed("too many GET RESPONSE rounds")
            }
            response = try transmit(
                PIVCodec.getResponseCommand(length: UInt8(response.status & 0xff))
            )
            result.append(response.payload)
        }
        guard response.status == 0x9000 else {
            throw PIVCodecError.status(response.status)
        }
        return result
    }

    private func transmit(_ command: Data) throws -> (payload: Data, status: UInt16) {
        let semaphore = DispatchSemaphore(value: 0)
        let lock = NSLock()
        var replyData: Data?
        var replyError: Error?
        smartCard.transmit(command) { response, error in
            lock.lock()
            replyData = response
            replyError = error
            lock.unlock()
            semaphore.signal()
        }
        guard semaphore.wait(timeout: .now() + 12) == .success else {
            throw PIVCodecError.malformed("smart-card request timed out")
        }
        lock.lock()
        defer { lock.unlock() }
        if let replyError {
            throw replyError
        }
        guard let replyData, replyData.count >= 2 else {
            throw PIVCodecError.malformed("smart-card response has no status")
        }
        let status = UInt16(replyData[replyData.count - 2]) << 8
            | UInt16(replyData[replyData.count - 1])
        return (replyData.dropLast(2), status)
    }
}
