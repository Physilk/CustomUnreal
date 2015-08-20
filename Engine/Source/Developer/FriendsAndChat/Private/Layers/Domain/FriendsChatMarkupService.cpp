// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#include "FriendsAndChatPrivatePCH.h"
#include "FriendsChatMarkupService.h"
#include "IChatCommunicationService.h"
#include "FriendsNavigationService.h"
#include "GameAndPartyService.h"
#include "FriendViewModel.h"
#include "FriendListViewModel.h"
#include "IFriendItem.h"
#include "IFriendList.h"

#define LOCTEXT_NAMESPACE ""

class FFriendsChatMarkupServiceFactoryFactory;
class FFriendsChatMarkupServiceFactoryImpl;

struct FProcessedInput
{
	FProcessedInput()
		: ChatChannel(EChatMessageType::Invalid)
		, NeedsTip(false)
		, FoundMatch(false)
	{}

	EChatMessageType::Type ChatChannel;
	FString Message;
	bool NeedsTip; // The input will only be executed when we no longer require a tip
	bool FoundMatch;
	TArray<TSharedPtr<FFriendViewModel> > ValidFriends;
	TArray<TSharedPtr<FFriendViewModel> > MatchedFriends;
	TArray<TSharedPtr<IChatTip> > ValidCustomTips;

	void Clear()
	{
		ValidFriends.Empty();
		MatchedFriends.Empty();
		ValidCustomTips.Empty();
		NeedsTip = false;
		FoundMatch = false;
		ChatChannel = EChatMessageType::Invalid;
	}
};

class FChatTip : public IChatTip
{
public:
	virtual FText GetTipText() override
	{
		FFormatNamedArguments Args;
		Args.Add(TEXT("ShortcutString"), FText::FromString(EChatMessageType::ShortcutString(ChatChannel)));
		Args.Add(TEXT("DisplayString"), EChatMessageType::ToText(ChatChannel));

		return FText::Format(NSLOCTEXT("FChatTip", "ChatTipText", "{ShortcutString} {DisplayString}"), Args);
	}

	virtual bool IsValidForType(EChatMessageType::Type InChatChannel) override
	{
		return this->ChatChannel == InChatChannel;
	}

	virtual bool IsEnabled() override
	{
		return true;
	}

	virtual FReply ExecuteTip() override
	{
		MarkupService->SetValidatedText(EChatMessageType::ShortcutString(ChatChannel));
		return FReply::Handled();
	}

	virtual void ExecuteCommand() override
	{
		// Do Nothing
	}

	FChatTip(const TSharedRef<FFriendsChatMarkupService>& InMarkupService, EChatMessageType::Type InChatChannel)
		: MarkupService(InMarkupService)
		, ChatChannel(InChatChannel)
		{}

	virtual ~FChatTip(){}

private:
	TSharedRef<FFriendsChatMarkupService> MarkupService;
	EChatMessageType::Type ChatChannel;
};

class FFriendChatTip : public IChatTip
{
public:
	virtual FText GetTipText() override
	{
		return FText::Format(LOCTEXT("MarkupFriendTip", "{0} {1}"), FText::FromString(EChatMessageType::ShortcutString(EChatMessageType::Whisper)), FText::FromString(FriendItem->GetName()));
	}

	virtual bool IsValidForType(EChatMessageType::Type ChatChannel) override
	{
		return true;
	}

	virtual bool IsEnabled() override
	{
		return true;
	}

	virtual FReply ExecuteTip() override
	{
		MarkupService->SetSelectedFriend(FriendItem);
		return FReply::Handled();
	}

	virtual void ExecuteCommand() override
	{
		// Do Nothing
	}

	FFriendChatTip(const TSharedRef<FFriendsChatMarkupService>& InMarkupService, TSharedRef<FFriendViewModel> InFriendItem)
		: MarkupService(InMarkupService)
		, FriendItem(InFriendItem)
		{}

	virtual ~FFriendChatTip(){}

private:
	TSharedRef<FFriendsChatMarkupService> MarkupService;
	TSharedRef<FFriendViewModel> FriendItem;
};

class FCustomActionChatTip : public IChatTip
{
public:
	virtual FText GetTipText() override
	{
		return FText::FromString(CustomSlashCommand->GetCommandToken());
	}

	virtual bool IsValidForType(EChatMessageType::Type ChatChannel) override
	{
		return true;
	}

	virtual bool IsEnabled() override
	{
		return CustomSlashCommand->IsEnabled();
	}

	virtual FReply ExecuteTip() override
	{
		MarkupService->SetValidatedText(CustomSlashCommand->GetCommandToken());
		return FReply::Handled();
	}

	virtual void ExecuteCommand() override
	{
		CustomSlashCommand->ExecuteCommand();
	}

	bool IsEnabled(const FString& NavigationToken)
	{
		if(CustomSlashCommand->GetCommandToken().ToUpper().Contains(NavigationToken.ToUpper()))
		{
			return CustomSlashCommand->IsEnabled();
		}
		return false;
	}

	bool CanExecute(const FString& NavigationToken)
	{
		return NavigationToken.ToUpper() == CustomSlashCommand->GetCommandToken().ToUpper();
	}

	FCustomActionChatTip(const TSharedRef<FFriendsChatMarkupService>& InMarkupService, TSharedRef<ICustomSlashCommand> InCustomSlashCommand)
		: MarkupService(InMarkupService)
		, CustomSlashCommand(InCustomSlashCommand)
		{}

	virtual ~FCustomActionChatTip(){}

private:
	TSharedRef<FFriendsChatMarkupService> MarkupService;
	TSharedRef<ICustomSlashCommand> CustomSlashCommand;
};

class FInvalidChatTip : public IChatTip
{
public:
	virtual FText GetTipText() override
	{
		if(InvalidReason.IsEmpty())
		{
			return LOCTEXT("InvalidChatTip", "Invalid markup");
		}
		return InvalidReason;
	}

	virtual bool IsValidForType(EChatMessageType::Type ChatChannel) override
	{
		return true;
	}

	virtual bool IsEnabled() override
	{
		return true;
	}

	virtual FReply ExecuteTip() override
	{
		return FReply::Handled();
	}

	virtual void ExecuteCommand() override
	{
		// Do Nothing
	}

	FInvalidChatTip(FText InInvalidReason)
	 : InvalidReason(InInvalidReason)
	{}

	FInvalidChatTip()
	 : InvalidReason(FText::GetEmpty())
	{}

	virtual ~FInvalidChatTip(){}

private:
	FText InvalidReason;
};


class FFriendsChatMarkupServiceImpl
	: public FFriendsChatMarkupService
{
public:

	virtual void AddCustomSlashMarkupCommand(TArray<TSharedRef<ICustomSlashCommand> >& InCustomSlashCommands) override
	{
		CustomSlashCommands = InCustomSlashCommands;
		CustomChatTips.Empty();
		for(const auto& ChatCommand : CustomSlashCommands)
		{
			CustomChatTips.Add(MakeShareable(new FCustomActionChatTip(SharedThis(this), ChatCommand)));
		}
	}

	virtual void CloseChatTips() override
	{
		ForceDisplayToolTips = false;
	}

	bool ValidateSlashMarkup(const FString NewMessage, const FString PlainText)
	{
		TSharedPtr<FProcessedInput> ValidatedInput = ProcessInputText(NewMessage, PlainText);

		if (ValidatedInput.IsValid())
		{
			if (ValidatedInput->ChatChannel == EChatMessageType::Global)
			{
				if(ValidatedInput->Message.IsEmpty())
				{
					NavigationService->ChangeChatChannel(EChatMessageType::Global);
				}
				else
				{
					CommunicationService->SendRoomMessage(FString(), ProcessedInput->Message);
				}
			}
			else if (ValidatedInput->ChatChannel == EChatMessageType::Party)
			{
				if(ValidatedInput->Message.IsEmpty())
				{
					NavigationService->ChangeChatChannel(EChatMessageType::Party);
				}
				else
				{
					FChatRoomId PartyChatRoomId = GamePartyService->GetPartyChatRoomId();
					CommunicationService->SendRoomMessage(PartyChatRoomId, ProcessedInput->Message);
				}
			}
			else if (ValidatedInput->ChatChannel == EChatMessageType::Whisper)
			{
				if(ValidatedInput->ValidFriends.Num())
				{
					if(ProcessedInput->Message.IsEmpty())
					{
						// Sending the selected outgoing friend
						NavigationService->SetOutgoingChatFriend(ProcessedInput->ValidFriends[0]->GetFriendItem());
						NavigationService->ChangeChatChannel(EChatMessageType::Whisper);
					}
					else
					{
						CommunicationService->SendPrivateMessage(ProcessedInput->ValidFriends[0]->GetFriendItem(), FText::FromString(ProcessedInput->Message));
						NavigationService->ChangeViewChannel(EChatMessageType::Whisper);
					}
				}
				else
				{
					// Change outgoing chat channel to last chat friend
					NavigationService->ChangeChatChannel(EChatMessageType::Whisper);
				}
			}
			else if(ValidatedInput->ChatChannel == EChatMessageType::Custom)
			{
				if(ValidatedInput->ValidCustomTips.Num() && ValidatedInput->ValidCustomTips[0].IsValid())
				{
					ValidatedInput->ValidCustomTips[0]->ExecuteCommand();
				}
			}
			else if (ValidatedInput->ChatChannel == EChatMessageType::Empty)
			{
				NavigationService->ChangeChatChannel(EChatMessageType::Empty);
			}
			else if(ValidatedInput->ChatChannel == EChatMessageType::Invalid)
			{
				// Handle bad text
			}
			else
			{
				NavigationService->ChangeChatChannel(ValidatedInput->ChatChannel);
			}
			return true;
		}
		return false;
	}

	virtual bool ChatTipAvailable() override
	{
		return ForceDisplayToolTips;
	}

	virtual void SetInputText(FString InInputText, FString InPlainText, bool InForceDisplayTip) override
	{
		this->InputText = InInputText;
		if(InputText == TEXT("/") && !InForceDisplayTip)
		{
			PauseTip = true;
			TipDisplayTime = 1;
			if (!TickDelegate.IsBound())
			{
				TickDelegate = FTickerDelegate::CreateSP(this, &FFriendsChatMarkupServiceImpl::HandleTick);
			}
			TickerHandle = FTicker::GetCoreTicker().AddTicker(TickDelegate);
		}
		else
		{
			PauseTip = false;
			TSharedPtr<FProcessedInput> ValidatedInput = ProcessInputText(InputText, InPlainText);
			if(ValidatedInput.IsValid() && ValidatedInput->MatchedFriends.Num() > 0)
			{
				SetSelectedFriend(ValidatedInput->MatchedFriends[0]);
				ForceDisplayToolTips = false;
			}

			if(InputText.StartsWith("/") && (!ValidatedInput.IsValid() || ValidatedInput->NeedsTip))
			{
				ForceDisplayToolTips = true;
			}
			else if(InputText.IsEmpty() || (ValidatedInput.IsValid() && !ValidatedInput->NeedsTip))
			{
				ForceDisplayToolTips = false;
			}
			GenerateTips();
		}
	}

	virtual void SetValidatedText(FString InInputText) override
	{
		this->InputText = InInputText;
		if(InputText.StartsWith("/") && ProcessedInput->NeedsTip)
		{
			ForceDisplayToolTips = true;
		}
		OnValidateInputReady().Broadcast();
	}

	virtual FText GetValidatedInput() override
	{
		return FText::FromString(InputText);
	}

	virtual void SetSelectedFriend(TSharedPtr<FFriendViewModel> FriendViewModel) override
	{
		if(FriendViewModel.IsValid())
		{
			FString HyperlinkStyle = TEXT("UserNameTextStyle.DefaultHyperlink");
			FFormatNamedArguments Args;
			Args.Add(TEXT("SenderName"), FText::FromString(FriendViewModel->GetName()));
			Args.Add(TEXT("UniqueID"), FText::FromString(FriendViewModel->GetFriendItem()->GetUniqueID()->ToString()));

			Args.Add(TEXT("NameStyle"), FText::FromString(HyperlinkStyle));

			FText ValidatedText = FText::Format(NSLOCTEXT("FFriendsChatMarkupService", "DisplayNameAndMessage", "/w <a id=\"UserName\" uid=\"{UniqueID}\" Username=\"{SenderName}\" style=\"{NameStyle}\">{SenderName}</> "), Args);
			ValidatedInputString = ValidatedText.ToString();
			SelectedFriendViewModel = FriendViewModel;
			ProcessedInput->ValidFriends.Empty();
			ProcessedInput->ValidFriends.Add(FriendViewModel);
			ProcessedInput->NeedsTip = false;
			ProcessedInput->ChatChannel = EChatMessageType::Whisper;
			SetValidatedText(ValidatedInputString);
		}
		else
		{
			ValidatedInputString.Empty();
			SelectedFriendViewModel = nullptr;
		}
		ProcessedInput->MatchedFriends.Empty();
	}

	virtual FReply HandleChatKeyEntry(const FKeyEvent& KeyEvent) override
	{
		FReply Reply = FReply::Unhandled();
		if(KeyEvent.GetKey() == EKeys::Tab || KeyEvent.GetKey() == EKeys::Enter)
		{
			if(SelectedChatTip.IsValid() && ProcessedInput.IsValid() && ProcessedInput->NeedsTip == true )
			{
				SelectedChatTip->ExecuteTip();
				Reply = FReply::Handled();
			}
		}
		else if(KeyEvent.GetKey() == EKeys::Up)
		{
			SetPreviousCommand();
			Reply = FReply::Handled();
		}
		else if( KeyEvent.GetKey() == EKeys::Down)
		{
			SetNextCommand();
			Reply = FReply::Handled();
		}
		else if(KeyEvent.GetKey() == EKeys::Escape)
		{
			ForceDisplayToolTips = false;
		}
		return Reply;
	}

	virtual TArray<TSharedRef<IChatTip> >& GetChatTips() override
	{
		return ChatTipArray;
	}

	virtual TSharedPtr<IChatTip> GetActiveTip() override
	{
		return SelectedChatTip;
	}

	DECLARE_DERIVED_EVENT(FFriendsChatMarkupServiceImpl, FFriendsChatMarkupService::FChatInputUpdated, FChatInputUpdated);
	virtual FChatInputUpdated& OnInputUpdated() override
	{
		return ChatInputUpdatedEvent;
	}

	DECLARE_DERIVED_EVENT(FFriendsChatMarkupServiceImpl, FFriendsChatMarkupService::FChatTipSelected, FChatTipSelected);
	virtual FChatTipSelected& OnChatTipSelected() override
	{
		return ChatTipSelectedEvent;
	}

	DECLARE_DERIVED_EVENT(FFriendsChatMarkupServiceImpl, FFriendsChatMarkupService::FValidatedChatReadyEvent, FValidatedChatReadyEvent);
	virtual FValidatedChatReadyEvent& OnValidateInputReady() override
	{
		return ValidatedChatReadyEvent;
	}

private:

	void GenerateTips()
	{
		SelectedChatTip.Reset();
		ChatTipArray.Empty();
		if(ProcessedInput->ChatChannel == EChatMessageType::Whisper)
		{
			if(ProcessedInput->ValidFriends.Num())
			{
				for(const auto& Friend : ProcessedInput->ValidFriends)
				{
					ChatTipArray.Add(MakeShareable(new FFriendChatTip(SharedThis(this), Friend.ToSharedRef())));
				}
			}
			else
			{
				ChatTipArray.Add(MakeShareable(new FInvalidChatTip(LOCTEXT("InvalidChatTip_NoFriends", "No matching friends currently online"))));
			}
		}
		else if(ProcessedInput->ChatChannel == EChatMessageType::Custom)
		{
			for(const auto& SlashCommand : ProcessedInput->ValidCustomTips)
			{
				if(SlashCommand->IsEnabled())
				{
					ChatTipArray.Add(SlashCommand.ToSharedRef());
				}
			}
		}
		else if(ProcessedInput->ChatChannel != EChatMessageType::Invalid)
		{
			for(const auto& ChatTip : CommmonChatTips)
			{
				if(ProcessedInput->ChatChannel == EChatMessageType::Empty || ChatTip->IsValidForType(ProcessedInput->ChatChannel))
				{
					if(ChatTip->IsValidForType(EChatMessageType::Party))
					{
						if(GamePartyService->IsInPartyChat())
						{
							ChatTipArray.Add(ChatTip); 
						}
					}
					else
					{
						ChatTipArray.Add(ChatTip); 
					}
				}
			}

			if(ProcessedInput->ChatChannel == EChatMessageType::Empty)
			{
				for(const auto& ChatTip : CustomChatTips)
				{
					if(ChatTip->IsEnabled())
					{
						ChatTipArray.Add(ChatTip); 
					}
				}
			}
		}
		else
		{
			ChatTipArray.Add(MakeShareable(new FInvalidChatTip()));
		}

		if(ChatTipArray.Num() > 0)
		{
			SelectedChatTip = ChatTipArray[0];
			SelectedChatIndex = 0;
		}
		OnInputUpdated().Broadcast();
	}

	void SetNextCommand()
	{
		if(SelectedChatTip.IsValid() && ChatTipArray.Num() > 0)
		{
			SelectedChatIndex++;
			if(SelectedChatIndex > ChatTipArray.Num() - 1)
			{
				SelectedChatIndex = 0;
			}
			SelectedChatTip = ChatTipArray[SelectedChatIndex];
			OnChatTipSelected().Broadcast(SelectedChatTip.ToSharedRef());
		}
	}

	void SetPreviousCommand()
	{
		if(SelectedChatTip.IsValid() && ChatTipArray.Num() > 0)
		{
			SelectedChatIndex--;
			if(SelectedChatIndex < 0)
			{
				SelectedChatIndex = ChatTipArray.Num() -1;
			}
			SelectedChatTip = ChatTipArray[SelectedChatIndex];
			OnChatTipSelected().Broadcast(SelectedChatTip.ToSharedRef());
		}
	}

	TSharedPtr<FProcessedInput> ProcessInputText(FString InInputText, FString InPlainText)
	{
		bool NeedsValidation = true;
		if(SelectedFriendViewModel.IsValid() && ProcessedInput.IsValid())
		{
			NeedsValidation = false;
			if(!InInputText.StartsWith(ValidatedInputString))
			{
				SetSelectedFriend(nullptr);
				SetValidatedText(InPlainText);
				NeedsValidation = true;
			}
			else
			{
				// Double check no friends names match
				ProcessedInput->Message = InInputText.RightChop(ValidatedInputString.Len());
				return ProcessedInput;
			}
		}

		if (NeedsValidation && InPlainText.StartsWith(TEXT("/")))
		{
			ProcessedInput->ChatChannel = EChatMessageType::Invalid;
			ProcessedInput->NeedsTip = true;
			ProcessedInput->ValidFriends.Empty();
			ProcessedInput->MatchedFriends.Empty();
			ProcessedInput->Message.Empty();

			FString NavigationToken, Remainder;
			if(InPlainText.Contains(TEXT(" ")))
			{
				InPlainText.Split(TEXT(" "), &NavigationToken, &Remainder);
			}
			else
			{
				NavigationToken = InPlainText;
			}

			EChatMessageType::Type ChatType = EChatMessageType::EnumFromString(NavigationToken);
			if(ChatType != EChatMessageType::Invalid)
			{
				ProcessedInput->ChatChannel = ChatType;
				if(ChatType == EChatMessageType::Whisper)
				{
					if(!Remainder.IsEmpty())
					{
						FString PotentialName = Remainder.Left(25).Replace(TEXT(" "), TEXT(""));

						for(const auto& Friend : GetFriendViewModels())
						{
							if(Remainder.EndsWith(TEXT(" ")) && Friend->GetNameNoSpaces() == PotentialName)
							{
								// Found match
								ProcessedInput->MatchedFriends.Add(Friend);
							}
							else if(Friend->GetNameNoSpaces().Left(PotentialName.Len()) == PotentialName)
							{
								ProcessedInput->ValidFriends.Add(Friend);
							}
						}
					}
					else
					{
						for(const auto& Friend : GetFriendViewModels())
						{
							ProcessedInput->ValidFriends.Add(Friend);
						}
					}
				}
				else if (ChatType == EChatMessageType::Party)
				{
					if(GamePartyService->IsInPartyChat())
					{
						ProcessedInput->NeedsTip = false;
						ProcessedInput->Message = InInputText.RightChop(NavigationToken.Len() + 1);
					}
					else
					{
						ProcessedInput->ChatChannel = EChatMessageType::Invalid;
					}
				}
				else if(ChatType != EChatMessageType::Empty)
				{
					ProcessedInput->NeedsTip = false;
					ProcessedInput->Message = InInputText.RightChop(NavigationToken.Len() + 1);
				}
			}
			else
			{
				ProcessedInput->ValidCustomTips.Empty();
				for (const auto& CustomChatTip : CustomChatTips)
				{
					if(CustomChatTip->IsEnabled(NavigationToken))
					{
						ProcessedInput->ChatChannel = EChatMessageType::Custom;
						ProcessedInput->ValidCustomTips.Add(CustomChatTip);
					}
					if(CustomChatTip->CanExecute(NavigationToken))
					{
						ProcessedInput->NeedsTip = false;
					}
				}
			}
			return ProcessedInput;
		}

		// Text is no longer markup - clear any existing tips
		ProcessedInput->Clear();
		return nullptr;
	}

	TArray< TSharedPtr<FFriendViewModel > > GetFriendViewModels()
	{
		// Can't create friends list on init yet as still depending on Friends singleton
		CreateFriendsList();
		return FriendViewModels;
	}

	TSharedPtr<FFriendViewModel> FindFriendViewModel(FString UserName)
	{
		for(const auto& Friend : FriendViewModels)
		{
			if(Friend->GetName() == UserName)
			{
				return Friend;
			}
		}
		return nullptr;
	}

	void CreateFriendsList()
	{
		if(!FriendsList.IsValid())
		{
			FriendsList = FriendsListFactory->Create(EFriendsDisplayLists::DefaultDisplay);
			FriendsList->OnFriendsListUpdated().AddSP(this, &FFriendsChatMarkupServiceImpl::HandleFriendListUpdated);
			HandleFriendListUpdated();
		}
	}

	void HandleFriendListUpdated()
	{
		FriendViewModels.Empty();
		TArray< TSharedPtr<FFriendViewModel > > AllFriendViewModels;
		FriendsList->GetFriendList(AllFriendViewModels);
		for(const auto& Friend : AllFriendViewModels)
		{
			const EOnlinePresenceState::Type PresenceState = Friend->GetOnlineStatus();
			if(PresenceState == EOnlinePresenceState::Online || PresenceState == EOnlinePresenceState::Away || PresenceState == EOnlinePresenceState::ExtendedAway)
			{
				FriendViewModels.Add(Friend);
			}
		}
	}

	bool HandleTick(float DeltaTime)
	{
		if(PauseTip)
		{
			TipDisplayTime -= DeltaTime;
			if(TipDisplayTime <= 0)
			{
				PauseTip = false;
				SetInputText(InputText, InputText, true);
			}
			return true;
		}

		return false;
	}

	void Initialize()
	{
		CommmonChatTips.Add(MakeShareable(new FChatTip(SharedThis(this), EChatMessageType::Party)));
		CommmonChatTips.Add(MakeShareable(new FChatTip(SharedThis(this), EChatMessageType::Whisper)));
		CommmonChatTips.Add(MakeShareable(new FChatTip(SharedThis(this), EChatMessageType::Global)));
		ProcessedInput = MakeShareable(new FProcessedInput());
	}

	FFriendsChatMarkupServiceImpl(
			const TSharedRef<IChatCommunicationService >& InCommunicationService,
			const TSharedRef<FFriendsNavigationService>& InNavigationService,
			const TSharedRef<IFriendListFactory>& InFriendsListFactory,
			const TSharedRef<FGameAndPartyService>& InGamePartyService
			)
		: CommunicationService(InCommunicationService)
		, NavigationService(InNavigationService)
		, FriendsListFactory(InFriendsListFactory)
		, GamePartyService(InGamePartyService)
		, InputText()
		, ForceDisplayToolTips(false)
		{}

private:
	TSharedRef<IChatCommunicationService > CommunicationService;
	TSharedRef<FFriendsNavigationService > NavigationService;
	TSharedRef<IFriendListFactory> FriendsListFactory;
	TSharedRef<FGameAndPartyService> GamePartyService;
	TArray<TSharedRef<ICustomSlashCommand> > CustomSlashCommands;
	TSharedPtr<IFriendList> FriendsList;
	TArray< TSharedPtr<FFriendViewModel > > FriendViewModels;
	TArray<TSharedRef<IChatTip> > CommmonChatTips;
	TArray<TSharedRef<FCustomActionChatTip> > CustomChatTips;
	FChatInputUpdated ChatInputUpdatedEvent;
	FChatTipSelected ChatTipSelectedEvent;
	FValidatedChatReadyEvent ValidatedChatReadyEvent;
	FString InputText;
	bool ForceDisplayToolTips;
	TSharedPtr<FFriendViewModel> SelectedFriendViewModel;
	FString ValidatedInputString;
	TSharedPtr<FProcessedInput> ProcessedInput;
	// Delegate for which function we should use when we tick
	FTickerDelegate TickDelegate;
	// Handler for the tick function when active
	FDelegateHandle TickerHandle;
	bool PauseTip;
	float TipDisplayTime;
	TArray<TSharedRef<IChatTip> > ChatTipArray;
	TSharedPtr<IChatTip> SelectedChatTip;
	int32 SelectedChatIndex;

	friend FFriendsChatMarkupServiceFactoryImpl;
};

class FFriendsChatMarkupServiceFactoryImpl
	: public IFriendsChatMarkupServiceFactory
{
public:
	virtual TSharedRef<FFriendsChatMarkupService> Create() override
	{
		TSharedRef<FFriendsChatMarkupServiceImpl> Service = MakeShareable(
			new FFriendsChatMarkupServiceImpl(CommunicationService, NavigationService,FriendsListFactory,GamePartyService));
		Service->Initialize();
		return Service;
	}

	virtual ~FFriendsChatMarkupServiceFactoryImpl(){}

private:

	FFriendsChatMarkupServiceFactoryImpl(
		const TSharedRef<IChatCommunicationService >& InCommunicationService,
		const TSharedRef<FFriendsNavigationService>& InNavigationService,
		const TSharedRef<IFriendListFactory>& InFriendsListFactory,
		const TSharedRef<FGameAndPartyService>& InGamePartyService
		)
		: CommunicationService(InCommunicationService)
		, NavigationService(InNavigationService)
		, FriendsListFactory(InFriendsListFactory)
		, GamePartyService(InGamePartyService)
	{ }

private:

	TSharedRef<IChatCommunicationService > CommunicationService;
	TSharedRef<FFriendsNavigationService> NavigationService;
	TSharedRef<IFriendListFactory> FriendsListFactory;
	TSharedRef<FGameAndPartyService> GamePartyService;

	friend FFriendsChatMarkupServiceFactoryFactory;
};

TSharedRef< IFriendsChatMarkupServiceFactory > FFriendsChatMarkupServiceFactoryFactory::Create(
	const TSharedRef<IChatCommunicationService >& CommunicationService,
	const TSharedRef<FFriendsNavigationService>& NavigationService,
	const TSharedRef<IFriendListFactory>& FriendsListFactory,
	const TSharedRef<FGameAndPartyService>& GamePartyService)
{
	TSharedRef< FFriendsChatMarkupServiceFactoryImpl > Service = MakeShareable(new FFriendsChatMarkupServiceFactoryImpl(CommunicationService, NavigationService, FriendsListFactory, GamePartyService));
	return Service;
}
#undef LOCTEXT_NAMESPACE