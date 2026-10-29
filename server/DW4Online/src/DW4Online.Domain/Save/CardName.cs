using System.Text;

namespace DW4Online.Domain.Save;

/// <summary>
/// PLAYERNAME conversion (save::name_to_card / name_from_card): the card stores full-width forms,
/// U+FF01..U+FF5E for '!'..'~' and U+3000 for a space.
/// </summary>
public static class CardName
{
    public static string ToCard(string ascii)
    {
        var o = new StringBuilder(RecordCodec.NameChars);
        foreach (var c in ascii)
        {
            if (o.Length == RecordCodec.NameChars) break;
            o.Append(c == ' ' ? '　' : c > 0x20 && c < 0x7F ? (char)(c + 0xFEE0) : c);
        }
        return o.ToString();
    }

    /// <summary>The ASCII form shown on the website ('?' for characters outside the full-width ASCII block).</summary>
    public static string FromCard(string card)
    {
        var o = new StringBuilder(card.Length);
        foreach (var c in card)
        {
            if (c >= '！' && c <= '～') o.Append((char)(c - 0xFEE0));
            else if (c == '　') o.Append(' ');
            else if (c < 0x80) o.Append(c);
            else o.Append('?');
        }
        return o.ToString();
    }

    /// <summary>True for the code units the US name entry produces (full-width ASCII and the ideographic space).</summary>
    public static bool IsCardCharacter(char c) => c is >= '！' and <= '～' or '　';
}
