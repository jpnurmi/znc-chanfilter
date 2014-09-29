/*
 * Copyright (C) 2004-2014  See the AUTHORS file for details.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include <znc/Modules.h>
#include <znc/IRCNetwork.h>
#include <znc/Client.h>
#include <znc/Chan.h>
#include <znc/Nick.h>

class CChanFilterMod : public CModule
{
public:
    MODCONSTRUCTOR(CChanFilterMod)
    {
        AddHelpCommand();
        AddCommand("AddClient", static_cast<CModCommand::ModCmdFunc>(&CChanFilterMod::AddClientCommand), "<identifier>", "Add a client.");
        AddCommand("DelClient", static_cast<CModCommand::ModCmdFunc>(&CChanFilterMod::DelClientCommand), "<identifier>", "Delete a client.");
        AddCommand("ListClients", static_cast<CModCommand::ModCmdFunc>(&CChanFilterMod::ListClientsCommand), "", "List clients.");
    }

    void AddClientCommand(const CString& line)
    {
        if (!AddClient(line.Token(1))) {
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
        CTable table;
        table.AddColumn("Client");
        table.AddColumn("Active");
        table.AddColumn("Channels");
        for (MCString::iterator it = BeginNV(); it != EndNV(); ++it) {
            table.AddRow();
            table.SetCell("Client", it->first);
            table.SetCell("Active", CString(GetNetwork()->FindClient(it->first)));
            table.SetCell("Channels", it->second.Ellipsize(128));
        }
        if (table.empty())
            PutModule("No identified clients");
        else
            PutModule(table);
    }

    virtual void OnClientLogin()
    {
        AddClient(GetClient()->GetIdentifier());
    }

    virtual EModRet OnUserRaw(CString& line)
    {
        CClient* client = GetClient();
        const CString identifier = client->GetIdentifier();

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
        CString identifier = client.GetIdentifier();

        if (network && !identifier.empty()) {
            // discard message tags
            CString msg = line;
            if (msg.StartsWith("@"))
                msg = msg.Token(1, true);

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
            } else if (cmd.Equals("NOTICE")) {
                if (nick.NickEquals("ChanServ")) {
                    CString target = rest.Token(1).TrimPrefix_n(":[").TrimSuffix_n("]");
                    if (network->IsChan(target) && !HasChannel(identifier, target))
                        return HALT;
                }
                channel = rest.Token(0);
            } else if (cmd.Equals("PRIVMSG") || cmd.Equals("JOIN") || cmd.Equals("PART") || cmd.Equals("MODE") || cmd.Equals("KICK") || cmd.Equals("TOPIC")) {
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
    SCString GetChannels(const CString& identifier) const
    {
        SCString channels;
        GetNV(identifier).Split(",", channels);
        return channels;
    }

    bool HasChannel(const CString& identifier, const CString& channel) const
    {
        const SCString channels = GetChannels(identifier);
        return channels.find(channel.AsLower()) != channels.end();
    }

    void AddChannel(const CString& identifier, const CString& channel)
    {
        if (!identifier.empty()) {
            SCString channels = GetChannels(identifier);
            channels.insert(channel.AsLower());
            SetNV(identifier, CString(",").Join(channels.begin(), channels.end()));
        }
    }

    void DelChannel(const CString& identifier, const CString& channel)
    {
        if (!identifier.empty()) {
            SCString channels = GetChannels(identifier);
            channels.erase(channel.AsLower());
            SetNV(identifier, CString(",").Join(channels.begin(), channels.end()));
        }
    }

    bool AddClient(const CString& identifier)
    {
        if (!identifier.empty()) {
            SetNV(identifier, GetNV(identifier));
            return true;
        }
        return false;
    }

    bool DelClient(const CString& identifier)
    {
        if (!identifier.empty()) {
            DelNV(identifier);
            return true;
        }
        return false;
    }
};

template<> void TModInfo<CChanFilterMod>(CModInfo& Info)
{
    Info.SetWikiPage("chanfilter");
}

NETWORKMODULEDEFS(CChanFilterMod, "A channel filter module for ZNC")
