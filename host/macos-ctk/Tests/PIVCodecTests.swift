import Foundation

@main
private enum PIVCodecTests {
    private static var failures = 0

    private static func check(_ condition: @autoclosure () -> Bool, _ name: String) {
        if condition() {
            print("  ok  \(name)")
        } else {
            failures += 1
            print("FAIL  \(name)")
        }
    }

    static func main() {
        do {
            let digest = Data(0..<32)
            let command = try PIVCodec.signCommand(digest: digest)
            check(command.prefix(11) == Data([
                0x00, 0x87, 0x11, 0x9a, 0x26, 0x7c, 0x24, 0x82, 0x00, 0x81, 0x20,
            ]), "9A command preserves the firmware wire contract")
            check(command.suffix(32) == digest, "9A command carries the exact digest")

            var peer = Data([0x04])
            peer.append(Data(repeating: 0x5a, count: 64))
            let agreement = try PIVCodec.keyAgreementCommand(peerPublicKey: peer)
            check(agreement.prefix(11) == Data([
                0x00, 0x87, 0x11, 0x9d, 0x47, 0x7c, 0x45, 0x82, 0x00, 0x85, 0x41,
            ]), "9D command preserves the firmware wire contract")
            check(agreement.suffix(65) == peer,
                  "9D command carries the exact public key")

            let der = Data([0x30, 0x03, 0x01, 0x01, 0xff])
            var certificateObject = Data([0x53, 0x0c, 0x70, 0x05])
            certificateObject.append(der)
            certificateObject.append(Data([0x71, 0x01, 0x00, 0xfe, 0x00]))
            let parsedCertificate = try PIVCodec.certificate(from: certificateObject)
            check(parsedCertificate == der,
                  "certificate parser extracts tag 70")

            let signature = Data([0x30, 0x06, 0x02, 0x01, 0x01, 0x02, 0x01, 0x02])
            var dynamic = Data([0x7c, 0x0a, 0x82, 0x08])
            dynamic.append(signature)
            let parsedSignature = try PIVCodec.dynamicAuthenticationResult(from: dynamic)
            check(parsedSignature == signature,
                  "dynamic-authentication parser extracts tag 82")

            do {
                _ = try PIVCodec.signCommand(digest: Data(repeating: 0, count: 31))
                check(false, "wrong digest size fails closed")
            } catch {
                check(true, "wrong digest size fails closed")
            }
        } catch {
            failures += 1
            print("FAIL  unexpected codec error: \(error)")
        }

        if failures == 0 {
            print("RESULT PASS")
        } else {
            print("RESULT FAIL (\(failures))")
            exit(1)
        }
    }
}
