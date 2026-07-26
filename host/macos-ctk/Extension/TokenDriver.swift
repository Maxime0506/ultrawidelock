import CryptoTokenKit

final class TokenDriver: TKSmartCardTokenDriver, TKSmartCardTokenDriverDelegate {
    func tokenDriver(_ driver: TKSmartCardTokenDriver,
                     createTokenFor smartCard: TKSmartCard,
                     aid AID: Data?) throws -> TKSmartCardToken {
        try Token(smartCard: smartCard, aid: AID, tokenDriver: self)
    }
}
