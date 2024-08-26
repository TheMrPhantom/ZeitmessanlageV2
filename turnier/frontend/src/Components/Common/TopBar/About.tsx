import React, { useEffect, useState } from 'react'
import Button from '@mui/material/Button';
import Dialog from '@mui/material/Dialog';
import DialogActions from '@mui/material/DialogActions';
import DialogContent from '@mui/material/DialogContent';
import DialogContentText from '@mui/material/DialogContentText';
import DialogTitle from '@mui/material/DialogTitle';
import { Typography } from '@mui/material';
import style from './topbar.module.scss';
import SettingsLink from './SettingsLink';
import { AKTUELLES_RELEASE, BUILD_NUMBER, DATENSCHUTZ, GETREANKELISTE, IMPRESSUM, IMPRESSUM_DATENSCHUTZ, OFFENE_ISSUES, OK } from '../Internationalization/i18n';
import Buildnumber from '../../../BuildNumber.json'
import { doGetRequest } from '../StaticFunctions';
import { format } from 'react-string-format';

type Props = {
    isOpen: boolean,
    close: () => void
}

const About = (props: Props) => {
    const [openIssues, setopenIssues] = useState(null)
    const [releaseTag, setreleaseTag] = useState(null)
    const [releaseName, setreleaseName] = useState(null)

    useEffect(() => {
        doGetRequest("webhooks/releases").then(value => {
            if (value.code === 200) {
                setreleaseTag(value.content.releaseTag)
                setreleaseName(value.content.releaseMessage)
                setopenIssues(value.content.openIssues)
            }
        })
    }, [props.isOpen])

    return (
        <Dialog open={props.isOpen} onClose={props.close}>
            <DialogTitle>{IMPRESSUM_DATENSCHUTZ}</DialogTitle>
            <DialogContent>
                <DialogContentText>
                    <div className={style.aboutDialogContainer}>
                        {window.globalTS.ORGANISATION_NAME !== "" ? <div className={style.aboutDialogRow}>
                            <Typography variant="overline">{GETREANKELISTE} </Typography>
                            <Typography variant="h5">{window.globalTS.ORGANISATION_NAME}</Typography>
                        </div> : <></>}
                        <div>
                            <SettingsLink title={IMPRESSUM} link={window.globalTS.ABOUT_LINK} />
                            <SettingsLink title={DATENSCHUTZ} link={window.globalTS.PRIVACY_LINK} />
                        </div>
                        {window.globalTS.ADDITIONAL_INFORMATION !== "" ? <Typography>
                            {window.globalTS.ADDITIONAL_INFORMATION}
                        </Typography> : <></>}
                        {releaseName !== null && releaseTag !== null ? <Typography variant="overline">
                            {format(AKTUELLES_RELEASE, releaseTag, releaseName)}
                        </Typography> : <></>}
                        {openIssues !== null ? <Typography variant="overline">
                            {format(OFFENE_ISSUES, openIssues)}
                        </Typography> : <></>}
                        {window.globalTS.ADDITIONAL_INFORMATION !== "" ? <Typography variant="overline">
                            {format(BUILD_NUMBER, Buildnumber)}
                        </Typography> : <></>}
                    </div>
                </DialogContentText>
            </DialogContent>
            <DialogActions>
                <Button onClick={props.close}>{OK}</Button>
            </DialogActions>
        </Dialog>
    )
}

export default About