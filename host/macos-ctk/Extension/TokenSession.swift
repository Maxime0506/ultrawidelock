import CryptoTokenKit
import Foundation
import Security

final class TokenSession: TKSmartCardTokenSession, TKTokenSessionDelegate {
    func tokenSession(_ session: TKTokenSession,
                      supports operation: TKTokenOperation,
                      keyObjectID: Any,
                      algorithm: TKTokenKeyAlgorithm) -> Bool {
        guard let key = PIVKey(rawValue: keyObjectID as? String ?? "") else {
            return false
        }
        switch (key, operation) {
        case (.authentication, .signData):
            return algorithm.supportsAlgorithm(
                .ecdsaSignatureDigestX962SHA256
            )
        case (.keyManagement, .performKeyExchange):
            return algorithm.supportsAlgorithm(.ecdhKeyExchangeStandard)
        default:
            return false
        }
    }

    func tokenSession(_ session: TKTokenSession, sign dataToSign: Data,
                      keyObjectID: Any,
                      algorithm: TKTokenKeyAlgorithm) throws -> Data {
        guard PIVKey(rawValue: keyObjectID as? String ?? "") == .authentication,
              algorithm.supportsAlgorithm(.ecdsaSignatureDigestX962SHA256) else {
            throw PIVCodecError.unsupported("requested signature key or algorithm")
        }
        return try transport().sign(digest: dataToSign)
    }

    func tokenSession(_ session: TKTokenSession,
                      performKeyExchange otherPartyPublicKeyData: Data,
                      keyObjectID objectID: Any,
                      algorithm: TKTokenKeyAlgorithm,
                      parameters: TKTokenKeyExchangeParameters) throws -> Data {
        guard PIVKey(rawValue: objectID as? String ?? "") == .keyManagement,
              algorithm.supportsAlgorithm(.ecdhKeyExchangeStandard) else {
            throw PIVCodecError.unsupported("requested agreement key or algorithm")
        }
        return try transport().deriveSharedSecret(peerPublicKey: otherPartyPublicKeyData)
    }

    private func transport() throws -> PIVTransport {
        PIVTransport(smartCard: try getSmartCard())
    }
}
