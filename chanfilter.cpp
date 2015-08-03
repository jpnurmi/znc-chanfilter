/*
 * Copyright (C) 2015 J-P Nurmi
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <znc/Modules.h>
#include <znc/IRCNetwork.h>
#include <znc/IRCSock.h>
#include <znc/Client.h>
#include <znc/Chan.h>
#include <znc/Nick.h>

class CChanFilterMod : public CModule
{
public:
    MODCONSTRUCTOR(CChanFilterMod)
    {
        AddHelpCommand();
        AddCommand("AddClient", static_cast<CModCommand::ModCmdFunc>(&CChanFilterMod::OnAddClientCommand), "<identifier>", "Add a client.");
        AddCommand("DelClient", static_cast<CModCommand::ModCmdFunc>(&CChanFilterMod::OnDelClientCommand), "<identifier>", "Delete a client.");
        AddCommand("ListClients", static_cast<CModCommand::ModCmdFunc>(&CChanFilterMod::OnListClientsCommand), "", "List known clients and their hidden channels.");
        AddCommand("ListChans", static_cast<CModCommand::ModCmdFunc>(&CChanFilterMod::OnListChansCommand), "[client]", "List all channels of a client.");
        AddCommand("RestoreChans", static_cast<CModCommand::ModCmdFunc>(&CChanFilterMod::OnRestoreChansCommand), "[client]", "Restore the hidden channels of a client.");
    }

    void OnAddClientCommand(const CString& line);
    void OnDelClientCommand(const CString& line);
    void OnListClientsCommand(const CString& line);
    void OnListChansCommand(const CString& line);
    void OnRestoreChansCommand(const CString& line);

    virtual EModRet OnUserRaw(CString& line) override;
    virtual EModRet OnSendToClient(CString& line, CClient& client) override;

private:
    SCString GetHiddenChannels(const CString& identifier) const;
    bool IsChannelVisible(const CString& identifier, const CString& channel) const;
    void SetChannelVisible(const CString& identifier, const CString& channel, bool visible);

    bool AddClient(const CString& identifier);
    bool DelClient(const CString& identifier);
    bool HasClient(const CString& identifier);
};

void CChanFilterMod::OnAddClientCommand(const CString& line)
{
    const CString identifier = line.Token(1);
    if (identifier.empty()) {
        PutModule("Usage: AddClient <identifier>");
        return;
    }
    if (HasClient(identifier)) {
        PutModule("Client already exists: " + identifier);
        return;
    }
    AddClient(identifier);
    PutModule("Client added: " + identifier);
}

void CChanFilterMod::OnDelClientCommand(const CString& line)
{
    const CString identifier = line.Token(1);
    if (identifier.empty()) {
        PutModule("Usage: DelClient <identifier>");
        return;
    }
    if (!HasClient(identifier)) {
        PutModule("Unknown client: " + identifier);
        return;
    }
    DelClient(identifier);
    PutModule("Client removed: " + identifier);
}

void CChanFilterMod::OnListClientsCommand(const CString& line)
{
    const CString current = GetClient()->GetIdentifier();

    CTable table;
    table.AddColumn("Client");
    table.AddColumn("Connected");
    table.AddColumn("Hidden channels");
    for (MCString::iterator it = BeginNV(); it != EndNV(); ++it) {
        table.AddRow();
        if (it->first == current)
            table.SetCell("Client",  "*" + it->first);
        else
            table.SetCell("Client",  it->first);
        table.SetCell("Connected", CString(!GetNetwork()->FindClients(it->first).empty()));
        table.SetCell("Hidden channels", it->second.Ellipsize(128));
    }

    if (table.empty())
        PutModule("No identified clients");
    else
        PutModule(table);
}

void CChanFilterMod::OnListChansCommand(const CString& line)
{
    CString identifier = line.Token(1);
    if (identifier.empty())
        identifier = GetClient()->GetIdentifier();

    if (identifier.empty()) {
        PutModule("Unidentified client");
        return;
    }

    if (!HasClient(identifier)) {
        PutModule("Unknown client: " + identifier);
        return;
    }

    CTable table;
    table.AddColumn("Client");
    table.AddColumn("Channel");
    table.AddColumn("Status");

    for (CChan* channel : GetNetwork()->GetChans()) {
        table.AddRow();
        table.SetCell("Client", identifier);
        table.SetCell("Channel", channel->GetName());
        if (channel->IsDisabled())
            table.SetCell("Status", "Disabled");
        else if (channel->IsDetached())
            table.SetCell("Status", "Detached");
        else if (IsChannelVisible(identifier, channel->GetName()))
            table.SetCell("Status", "Visible");
        else
            table.SetCell("Status", "Hidden");
    }

    PutModule(table);
}

void CChanFilterMod::OnRestoreChansCommand(const CString& line)
{
    CString identifier = line.Token(1);
    if (identifier.empty())
        identifier = GetClient()->GetIdentifier();

    if (identifier.empty()) {
        PutModule("Unidentified client");
        return;
    }

    if (!HasClient(identifier)) {
        PutModule("Unknown client: " + identifier);
        return;
    }

    const SCString channels = GetHiddenChannels(identifier);
    if (channels.empty()) {
        PutModule("No hidden channels");
        return;
    }

    unsigned int count = 0;
    for (const CString& name : channels) {
        SetChannelVisible(identifier, name, true);
        CIRCNetwork* network = GetNetwork();
        CChan* channel = network->FindChan(name);
        if (channel) {
            for (CClient* client : network->FindClients(identifier))
                channel->AttachUser(client);
            ++count;
        }
    }
    PutModule("Restored " + CString(count) + " channels");
}

CModule::EModRet CChanFilterMod::OnUserRaw(CString& line)
{
    const CString identifier = GetClient()->GetIdentifier();
    if (HasClient(identifier)) {
        CIRCNetwork* network = GetNetwork();
        const CString cmd = line.Token(0);

        if (cmd.Equals("JOIN")) {
            // a join command from an identified client either
            // - restores a hidden channel and is filtered out,
            // - is let through so ZNC joins the channel,
            // - or "0" as a special case hides all channels (issue #2)
            const CString arg = line.Token(1);
            if (arg.Equals("0")) {
                for (CChan* channel : network->GetChans()) {
                    const CString name = channel->GetName();
                    if (channel->IsOn() && IsChannelVisible(identifier, name)) {
                        SetChannelVisible(identifier, name, false);
                        for (CClient* client : network->FindClients(identifier)) {
                            // use Write() instead of PutClient() to bypass OnSendToClient()
                            client->Write(":" + client->GetNickMask() + " PART " + name + "\r\n");
                        }
                    }
                }
                return HALT;
            }
            SetChannelVisible(identifier, arg, true);
            CChan* channel = network->FindChan(arg);
            if (channel) {
                for (CClient* client : network->FindClients(identifier))
                    channel->AttachUser(client);
                return HALT;
            }
        } else if (cmd.Equals("PART")) {
            // a part command from an identified client either
            // - hides a visible channel and is filtered out
            // - is let through so ZNC parts the channel
            const CString arg = line.Token(1);
            CChan* channel = network->FindChan(arg);
            if (channel && IsChannelVisible(identifier, arg)) {
                SetChannelVisible(identifier, arg, false);
                for (CClient* client : network->FindClients(identifier)) {
                    // use Write() instead of PutClient() to bypass OnSendToClient()
                    client->Write(":" + client->GetNickMask() + " PART " + arg + "\r\n");
                }
                return HALT;
            }
        }
    }
    return CONTINUE;
}

CModule::EModRet CChanFilterMod::OnSendToClient(CString& line, CClient& client)
{
    EModRet ret = CONTINUE;
    CIRCNetwork* network = client.GetNetwork();
    const CString identifier = client.GetIdentifier();

    if (network && HasClient(identifier)) {
        // discard message tags
        CString msg = line;
        if (msg.StartsWith("@"))
            msg = msg.Token(1, true);

        const CNick nick(msg.Token(0).TrimPrefix_n());
        const CString cmd = msg.Token(1);
        const CString rest = msg.Token(2, true);

        // identify the channel token from (possibly) channel specific messages
        CString channel;
        if (cmd.length() == 3 && isdigit(cmd[0]) && isdigit(cmd[1]) && isdigit(cmd[2])) {
            // must block the following numeric replies that are automatically sent on attach:
            // RPL_NAMREPLY, RPL_ENDOFNAMES, RPL_TOPIC, RPL_TOPICWHOTIME...
            unsigned int num = cmd.ToUInt();
            if (num == 353) // RPL_NAMREPLY
                channel = rest.Token(2);
            else
                channel = rest.Token(1);
        } else if (cmd.Equals("PRIVMSG") || cmd.Equals("NOTICE") || cmd.Equals("JOIN") || cmd.Equals("PART") || cmd.Equals("MODE") || cmd.Equals("KICK") || cmd.Equals("TOPIC")) {
            channel = rest.Token(0).TrimPrefix_n(":");
        }

        // remove status prefix (#1)
        CIRCSock* sock = client.GetIRCSock();
        if (sock)
            channel.TrimLeft(sock->GetISupport("STATUSMSG", ""));

        // filter out channel specific messages for hidden channels
        if (network->IsChan(channel) && !IsChannelVisible(identifier, channel))
            ret = HALTCORE;

        // a self part message from znc to an identified client must
        // be ignored if the client has already quit/closed connection,
        // otherwise clear the visibility status
        if (cmd.Equals("PART") && client.IsConnected() && !client.IsClosed() && nick.GetNick().Equals(client.GetNick()))
            SetChannelVisible(identifier, channel, true);
    }
    return ret;
}

SCString CChanFilterMod::GetHiddenChannels(const CString& identifier) const
{
    SCString channels;
    GetNV(identifier).Split(",", channels);
    return channels;
}

bool CChanFilterMod::IsChannelVisible(const CString& identifier, const CString& channel) const
{
    const SCString channels = GetHiddenChannels(identifier);
    return channels.find(channel.AsLower()) == channels.end();
}

void CChanFilterMod::SetChannelVisible(const CString& identifier, const CString& channel, bool visible)
{
    if (!identifier.empty()) {
        SCString channels = GetHiddenChannels(identifier);
        if (visible)
            channels.erase(channel.AsLower());
        else
            channels.insert(channel.AsLower());
        SetNV(identifier, CString(",").Join(channels.begin(), channels.end()));
    }
}

bool CChanFilterMod::AddClient(const CString& identifier)
{
    return SetNV(identifier, GetNV(identifier));
}

bool CChanFilterMod::DelClient(const CString& identifier)
{
    return DelNV(identifier);
}

bool CChanFilterMod::HasClient(const CString& identifier)
{
    return !identifier.empty() && FindNV(identifier) != EndNV();
}

template<> void TModInfo<CChanFilterMod>(CModInfo& Info)
{
    Info.SetWikiPage("chanfilter");
}

NETWORKMODULEDEFS(CChanFilterMod, "A channel filter for identified clients")
