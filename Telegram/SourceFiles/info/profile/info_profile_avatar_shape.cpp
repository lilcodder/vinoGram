#include "info/profile/info_profile_avatar_shape.h"

#include "core/click_handler_types.h"
#include "data/components/avatar_shapes.h"
#include "data/data_peer.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/effects/expressive_avatar.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/painter.h"
#include "ui/userpic_view.h"

#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_widgets.h"

namespace Info::Profile {

object_ptr<Ui::BoxContent> AvatarShapeBox(
		not_null<Main::Session*> session) {
	return Box([=](not_null<Ui::GenericBox*> box) {
		auto &manager = session->avatarShapes();
		box->setTitle(rpl::single(u"Форма аватарки"_q));
		box->setWidth(st::boxWideWidth);
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			rpl::single(u"Форма видна только в профиле у пользователей vinoGram. В чатах аватарка остаётся круглой."_q),
			st::boxLabel));
		const auto selected = box->lifetime().make_state<Data::AvatarShape>(
			manager.shape(session->userId()));
		const auto preview = box->addRow(object_ptr<Ui::FixedHeightWidget>(
			box, 150));
		const auto view = preview->lifetime().make_state<Ui::PeerUserpicView>();
		preview->paintRequest() | rpl::on_next([=] {
			auto painter = QPainter(preview);
			const auto rect = QRect((preview->width() - 128) / 2, 8, 128, 128);
			const auto image = PeerData::GenerateUserpicImage(
				session->user(), *view, 128 * style::DevicePixelRatio(), 0);
			Ui::PaintProfileAvatar(painter, rect, image, *selected);
		}, preview->lifetime());
		session->downloaderTaskFinished() | rpl::on_next([=] {
			preview->update();
		}, preview->lifetime());
		const auto group = std::make_shared<Ui::RadiobuttonGroup>(int(*selected));
		const auto option = [&](Data::AvatarShape shape, const QString &title) {
			box->addRow(object_ptr<Ui::Radiobutton>(
				box, group, int(shape), title));
		};
		option(Data::AvatarShape::Circle, u"Круг"_q);
		option(Data::AvatarShape::RoundedSquare, u"Скруглённый квадрат"_q);
		option(Data::AvatarShape::Flower, u"Цветок"_q);
		if (session->userId() == Data::kExpressiveAvatarUser) {
			option(Data::AvatarShape::Expressive, u"Material 3 Expressive — анимация"_q);
		}
		group->setChangedCallback([=](int value) {
			*selected = Data::AvatarShape(value);
			preview->update();
		});
		const auto status = box->addRow(object_ptr<Ui::FlatLabel>(
			box, rpl::single(manager.status()), st::boxLabel));
		const auto connect = box->addRow(object_ptr<Ui::RoundButton>(
			box, rpl::single(u"Подтвердить аккаунт через Telegram"_q),
			st::defaultActiveButton));
		connect->setClickedCallback([=] {
			session->avatarShapes().authorize([](QString url) {
				UrlClickHandler::Open(url);
			});
		});
		const auto save = box->addButton(rpl::single(u"Сохранить"_q), [=] {
			session->avatarShapes().save(*selected);
		});
		const auto update = [=] {
			auto &manager = session->avatarShapes();
			connect->setVisible(!manager.authorized());
			connect->setDisabled(manager.busy());
			save->setDisabled(manager.busy());
			status->setText(manager.status().isEmpty()
				? u"Выбери форму. Для синхронизации нужно один раз подтвердить свой аккаунт."_q
				: manager.status());
		};
		manager.updates() | rpl::on_next(update, box->lifetime());
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
		box->boxClosing() | rpl::on_next([=] {
			session->avatarShapes().cancelAuthorization();
		}, box->lifetime());
		update();
	});
}

} // namespace Info::Profile
