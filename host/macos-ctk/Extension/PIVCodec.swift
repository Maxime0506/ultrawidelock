import Foundation

enum PIVCodecError: LocalizedError {
    case malformed(String)
    case status(UInt16)
    case unsupported(String)

    var errorDescription: String? {
        switch self {
        case .malformed(let detail):
            return "Malformed PIV response: \(detail)"
        case .status(let status):
            return String(format: "PIV command failed with status %04X", status)
        case .unsupported(let detail):
            return "Unsupported PIV operation: \(detail)"
        }
    }
}

enum PIVObject {
    static let authenticationCertificate = Data([0x5f, 0xc1, 0x05])
    static let keyManagementCertificate = Data([0x5f, 0xc1, 0x0b])
}

enum PIVKey: String {
    case authentication = "piv-9a"
    case keyManagement = "piv-9d"

    var reference: UInt8 {
        switch self {
        case .authentication:
            return 0x9a
        case .keyManagement:
            return 0x9d
        }
    }
}

enum PIVCodec {
    private struct TLV {
        let tag: UInt32
        let value: Data
    }

    static func getDataCommand(for object: Data) -> Data {
        var body = Data([0x5c, UInt8(object.count)])
        body.append(object)
        return shortCommand(ins: 0xcb, p1: 0x3f, p2: 0xff, data: body)
    }

    static func getResponseCommand(length: UInt8) -> Data {
        Data([0x00, 0xc0, 0x00, 0x00, length])
    }

    static func signCommand(digest: Data) throws -> Data {
        guard digest.count == 32 else {
            throw PIVCodecError.unsupported("P-256 signing requires a 32-byte SHA-256 digest")
        }
        var inner = Data([0x82, 0x00, 0x81, UInt8(digest.count)])
        inner.append(digest)
        let body = tlv(tag: 0x7c, value: inner)
        return shortCommand(ins: 0x87, p1: 0x11, p2: PIVKey.authentication.reference,
                            data: body)
    }

    static func keyAgreementCommand(peerPublicKey: Data) throws -> Data {
        guard peerPublicKey.count == 65, peerPublicKey.first == 0x04 else {
            throw PIVCodecError.unsupported(
                "P-256 key agreement requires a 65-byte uncompressed public key"
            )
        }
        var inner = Data([0x82, 0x00, 0x85, UInt8(peerPublicKey.count)])
        inner.append(peerPublicKey)
        let body = tlv(tag: 0x7c, value: inner)
        return shortCommand(ins: 0x87, p1: 0x11,
                            p2: PIVKey.keyManagement.reference, data: body)
    }

    static func certificate(from object: Data) throws -> Data {
        let outer = try parseOne(object)
        guard outer.tag == 0x53 else {
            throw PIVCodecError.malformed("certificate object is not tag 53")
        }
        for item in try parseAll(outer.value) where item.tag == 0x70 {
            guard !item.value.isEmpty else {
                throw PIVCodecError.malformed("tag 70 contains no certificate")
            }
            return item.value
        }
        throw PIVCodecError.malformed("certificate object has no tag 70")
    }

    static func dynamicAuthenticationResult(from response: Data) throws -> Data {
        let outer = try parseOne(response)
        guard outer.tag == 0x7c else {
            throw PIVCodecError.malformed("authentication response is not tag 7C")
        }
        for item in try parseAll(outer.value) where item.tag == 0x82 {
            guard !item.value.isEmpty else {
                throw PIVCodecError.malformed("tag 82 contains no result")
            }
            return item.value
        }
        throw PIVCodecError.malformed("authentication response has no tag 82")
    }

    private static func shortCommand(ins: UInt8, p1: UInt8, p2: UInt8,
                                     data: Data) -> Data {
        precondition(data.count <= 255)
        var command = Data([0x00, ins, p1, p2, UInt8(data.count)])
        command.append(data)
        return command
    }

    private static func tlv(tag: UInt8, value: Data) -> Data {
        var result = Data([tag])
        result.append(encodedLength(value.count))
        result.append(value)
        return result
    }

    private static func encodedLength(_ length: Int) -> Data {
        precondition(length >= 0 && length <= 0xffff)
        if length < 0x80 {
            return Data([UInt8(length)])
        }
        if length <= 0xff {
            return Data([0x81, UInt8(length)])
        }
        return Data([0x82, UInt8(length >> 8), UInt8(length & 0xff)])
    }

    private static func parseOne(_ data: Data) throws -> TLV {
        var offset = 0
        let item = try parse(data, offset: &offset)
        guard offset == data.count else {
            throw PIVCodecError.malformed("trailing bytes after object")
        }
        return item
    }

    private static func parseAll(_ data: Data) throws -> [TLV] {
        var offset = 0
        var result: [TLV] = []
        while offset < data.count {
            result.append(try parse(data, offset: &offset))
        }
        return result
    }

    private static func parse(_ data: Data, offset: inout Int) throws -> TLV {
        guard offset < data.count else {
            throw PIVCodecError.malformed("missing tag")
        }
        var tag = UInt32(data[offset])
        offset += 1
        if tag & 0x1f == 0x1f {
            repeat {
                guard offset < data.count else {
                    throw PIVCodecError.malformed("truncated multi-byte tag")
                }
                let byte = data[offset]
                offset += 1
                tag = (tag << 8) | UInt32(byte)
                if byte & 0x80 == 0 {
                    break
                }
            } while true
        }

        guard offset < data.count else {
            throw PIVCodecError.malformed("missing length")
        }
        let firstLength = Int(data[offset])
        offset += 1
        let length: Int
        if firstLength & 0x80 == 0 {
            length = firstLength
        } else {
            let byteCount = firstLength & 0x7f
            guard byteCount > 0, byteCount <= 2,
                  offset + byteCount <= data.count else {
                throw PIVCodecError.malformed("unsupported or truncated length")
            }
            var decoded = 0
            for _ in 0..<byteCount {
                decoded = (decoded << 8) | Int(data[offset])
                offset += 1
            }
            length = decoded
        }
        guard length >= 0, offset + length <= data.count else {
            throw PIVCodecError.malformed("value exceeds response")
        }
        let value = data.subdata(in: offset..<(offset + length))
        offset += length
        return TLV(tag: tag, value: value)
    }
}
