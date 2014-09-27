/*
 * Copyright (C) 2004-2014  See the AUTHORS file for details.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include <znc/Modules.h>
#include <znc/IRCNetwork.h>
#include <znc/ZNCString.h>
#include <znc/Client.h>
#include <znc/Chan.h>
#include <znc/Nick.h>

class CClientIDMod : public CModule
{
public:
    MODCONSTRUCTOR(CClientIDMod)
    {
        AddHelpCommand();
        AddCommand("AddClient", static_cast<CModCommand::ModCmdFunc>(&CClientIDMod::AddClientCommand), "<identifier>", "Add a client.");
        AddCommand("DelClient", static_cast<CModCommand::ModCmdFunc>(&CClientIDMod::DelClientCommand), "<identifier>", "Delete a client.");
        AddCommand("ListClients", static_cast<CModCommand::ModCmdFunc>(&CClientIDMod::ListClientsCommand), "", "List clients.");
    }

    void AddClientCommand(const CString& line)
    {
        if (!AddClient(line.Token(1), NULL)) {
            PutModule("Usage: AddClient <identifier>");
            return;
        }
        ListClientsCommand();
    }

    void DelClientCommand(const CString& line)
    {
        if (!DelClient(line.Token(1))) {
            PutModule("Usage: DelClient <identifier>");
            return;
        }
        ListClientsCommand();
    }

    void ListClientsCommand(const CString& = CString())
    {
        if (m_clients.empty()) {
            PutModule("No clients");
        } else {
            CTable Table;
            Table.AddColumn("Client");
            Table.AddColumn("Active");
            Table.AddColumn("Channels");
            for (const auto& it : m_clients) {
                const SCString channels = GetChannels(it.first);
                Table.AddRow();
                Table.SetCell("Client", it.first);
                Table.SetCell("Active", CString(it.second));
                Table.SetCell("Channels", CString(",").Join(channels.begin(), channels.end()));
            }
            PutModule(Table);
        }
    }

    virtual void OnClientDisconnect()
    {
        AddClient(GetIdentifier(GetClient()), NULL);
    }

    virtual EModRet OnUnknownUserRaw(CClient* client, CString& line)
    {
        const CString cmd = line.Token(0);
        if (cmd.Equals("PASS")) {
            // [user[@identifier][/network]:]password
            CString auth = line.Token(1, true).TrimPrefix_n();
            const CString user = TakePrefix(auth, "@", false);
            if (!user.empty()) {
                CString identifier = TakePrefix(auth, "/", true);
                if (identifier.empty())
                    identifier = TakePrefix(auth, ":", true);
                if (AddClient(identifier, client))
                    line = cmd + " " + user + auth;
            }
        } else if (cmd.Equals("USER")) {
            // user[@identifier][/network]
            CString auth = line.Token(1, true).TrimPrefix_n();
            const CString user = TakePrefix(auth, "@", false);
            if (!user.empty()) {
                const CString identifier = TakePrefix(auth, "/", true);
                if (AddClient(identifier, client))
                    line = cmd + " " + user + auth;
            }
        }
        return CONTINUE;
    }

    virtual EModRet OnUserRaw(CString& line)
    {
        CClient* client = GetClient();
        const CString identifier = GetIdentifier(client);

        if (!identifier.empty()) {
            const CString cmd = line.Token(0);
            if (cmd.Equals("JOIN")) {
                const CString name = line.Token(1);
                AddChannel(identifier, name);
                CChan* channel = client->GetNetwork()->FindChan(name);
                if (channel) {
                    channel->JoinUser(true, "", client);
                    return HALT;
                }
            } else if (cmd.Equals("PART")) {
                const CString name = line.Token(1);
                DelChannel(identifier, name);
                // bypass OnUserRaw()
                client->Write(":" + client->GetNickMask() + " PART " + name + "\r\n");
                return HALT;
            }
        }
        return CONTINUE;
    }

    virtual EModRet OnSendToClient(CString& line, CClient& client)
    {
        EModRet ret = CONTINUE;
        CIRCNetwork* network = client.GetNetwork();
        CString identifier = GetIdentifier(&client);

        if (network && !identifier.empty()) {
            // discard message tags
            CString msg = line;
            if (msg.StartsWith("@"))
                TakePrefix(msg, ";", false);

            const CNick nick(msg.Token(0).TrimPrefix_n());
            const CString cmd = msg.Token(1);
            const CString rest = msg.Token(2, true);

            if (cmd.Equals("QUIT") || cmd.Equals("NICK")) {
                const SCString channels = GetChannels(identifier);
                for (const CString& name : channels) {
                    const CChan* channel = network->FindChan(name);
                    if (channel && channel->FindNick(nick.GetNick()))
                        return CONTINUE;
                }
            }

            CString channel;
            if (cmd.length() == 3 && isdigit(cmd[0]) && isdigit(cmd[1]) && isdigit(cmd[2])) {
                unsigned int num = cmd.ToUInt();
                if (num == 353) // RPL_NAMES
                    channel = rest.Token(2);
                else
                    channel = rest.Token(1);
            } else if (cmd.Equals("PRIVMSG") || cmd.Equals("NOTICE") || cmd.Equals("JOIN") || cmd.Equals("PART") || cmd.Equals("MODE") || cmd.Equals("KICK") || cmd.Equals("TOPIC")) {
                channel = rest.Token(0);
            }
            channel.TrimPrefix(":");

            if (network->IsChan(channel) && !HasChannel(identifier, channel))
                ret = HALT;

            if (cmd.Equals("PART") && nick.GetNick().Equals(client.GetNick()))
                DelChannel(identifier, channel);
        }
        return ret;
    }

private:
    CString GetIdentifier(CClient* client) const
    {
        for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
            if (it->second == client)
                return it->first;
        }
        return CString();
    }

    SCString GetChannels(const CString& identifier) const
    {
        auto it = m_channels.find(identifier);
        if (it != m_channels.end())
            return it->second;
        return SCString();
    }

    bool HasChannel(const CString& identifier, const CString& channel) const
    {
        const SCString channels = GetChannels(identifier);
        return channels.find(channel.AsLower()) != channels.end();
    }

    void AddChannel(const CString& identifier, const CString& channel)
    {
        if (!identifier.empty())
            m_channels[identifier].insert(channel.AsLower());
    }

    void DelChannel(const CString& identifier, const CString& channel)
    {
        if (!identifier.empty())
            m_channels[identifier].erase(channel.AsLower());
    }

    bool AddClient(const CString& identifier, CClient* client)
    {
        if (!identifier.empty()) {
            m_clients[identifier.AsLower()] = client;
            return true;
        }
        return false;
    }

    bool DelClient(const CString& identifier)
    {
        if (!identifier.empty()) {
            auto it = m_clients.find(identifier.AsLower());
            if (it != m_clients.end())
                m_clients.erase(it);
            return true;
        }
        return false;
    }

    static CString TakePrefix(CString& line, const CString& separator, bool retain)
    {
        CString prefix;
        if (line.find(separator) != CString::npos) {
            prefix = line.Token(0, false, separator);
            line = line.Token(1, true, separator);
            if (retain)
                line.insert(0, separator);
        }
        return prefix;
    }

    std::map<CString, CClient*> m_clients;
    std::map<CString, SCString> m_channels;
};

template<> void TModInfo<CClientIDMod>(CModInfo& Info)
{
    Info.SetWikiPage("clientid");
}

GLOBALMODULEDEFS(CClientIDMod, "A client ID module for ZNC")
