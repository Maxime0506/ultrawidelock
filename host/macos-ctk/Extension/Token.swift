import CryptoKit
import CryptoTokenKit
import Foundation
import Security

final class Token: TKSmartCardToken, TKTokenDelegate {
    init(smartCard: TKSmartCard, aid AID: Data?,
         tokenDriver: TKSmartCardTokenDriver) throws {
        let transport = PIVTransport(smartCard: smartCard)
        let authDER = try transport.readCertificate(
            object: PIVObject.authenticationCertificate
        )
        let keyManagementDER = try transport.readCertificate(
            object: PIVObject.keyManagementCertificate
        )
        let instanceID = SHA256.hash(data: authDER).prefix(16)
            .map { String(format: "%02x", $0) }.joined()

        super.init(smartCard: smartCard, aid: AID, instanceID: instanceID,
                   tokenDriver: tokenDriver)

        let authItems = try Self.items(
            certificateDER: authDER,
            key: .authentication,
            label: "OpenAliro Presence PIV",
            login: true
        )
        let keyManagementItems = try Self.items(
            certificateDER: keyManagementDER,
            key: .keyManagement,
            label: "OpenAliro Presence PIV Key Management",
            login: false
        )
        keychainContents?.fill(with: authItems + keyManagementItems)
        delegate = self
    }

    func createSession(_ token: TKToken) throws -> TKTokenSession {
        let session = TokenSession(token: self)
        session.delegate = session
        return session
    }

    private static func items(certificateDER: Data, key: PIVKey, label: String,
                              login: Bool) throws -> [TKTokenKeychainItem] {
        guard let certificate = SecCertificateCreateWithData(
            nil, certificateDER as CFData
        ) else {
            throw PIVCodecError.malformed("\(key.rawValue) is not an X.509 certificate")
        }
        guard let certificateItem = TKTokenKeychainCertificate(
            certificate: certificate,
            objectID: "\(key.rawValue)-certificate" as NSString
        ), let keyItem = TKTokenKeychainKey(
            certificate: certificate,
            objectID: key.rawValue as NSString
        ) else {
            throw PIVCodecError.malformed("CryptoTokenKit rejected \(key.rawValue)")
        }

        certificateItem.label = label
        keyItem.label = label
        keyItem.keyType = kSecAttrKeyTypeECSECPrimeRandom as String
        keyItem.keySizeInBits = 256
        keyItem.canSign = key == .authentication
        keyItem.canPerformKeyExchange = key == .keyManagement
        keyItem.isSuitableForLogin = login

        let operation: TKTokenOperation = key == .authentication
            ? .signData : .performKeyExchange
        keyItem.constraints = [NSNumber(value: operation.rawValue): true]
        return [certificateItem, keyItem]
    }
}
